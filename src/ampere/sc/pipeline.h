//
// Copyright (c) 2018 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <cassert>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <stdlib.h> // aligned_alloc

#include <boost/circular_buffer.hpp>
#include <boost/iterator/iterator_facade.hpp>
#include <systemc>

#include "ampere/sc/ancestor.h"
#include "ampere/sc/environment.h"
#include "ampere/sc/module.h"
#include "ampere/sc/param.h"
#include "ampere/sc/report.h"
#include "ampere/sc/fifo/fifo.h"

namespace ampere::sc {

template<typename T>
class FreeList
{
public:
    explicit FreeList(size_t size);
    FreeList() = delete;
    FreeList(const FreeList&) = delete;
    ~FreeList();

    void resize(size_t size);
    T* allocate();
    void free(T* data);

private:
    char* base{nullptr};
    std::set<T*> allowed;
    std::vector<T*> free_list;
};

template<typename T>
FreeList<T>::FreeList(size_t size)
{
    resize(size);
}

template<typename T>
FreeList<T>::~FreeList()
{
    std::free(base);
}

template<typename T>
void FreeList<T>::resize(size_t size)
{
    assert(free_list.size() == allowed.size()); // Must free all outstanding allocations prior to calling resize
    free_list.clear();
    allowed.clear();
    if (base) {
        std::free(base);
        base = nullptr;
    }

    if (size == 0)
        return;

    base = static_cast<char*>(aligned_alloc(alignof(T), sizeof(T) * size));
    for (size_t i = 0; i < size; i++) {
        T* ptr = reinterpret_cast<T*>(base + (sizeof(T) * i));
        free_list.push_back(ptr);
        allowed.insert(ptr);
    }
}

template<typename T>
T* FreeList<T>::allocate()
{
    assert(!free_list.empty());
    auto ret = free_list.back();
    free_list.pop_back();
    return ret;
}

template<typename T>
void FreeList<T>::free(T* data)
{
#ifndef NDEBUG
    for (auto& d : free_list)
        assert(d != data);
#endif
    assert(allowed.count(data));
    free_list.push_back(data);
}

template<typename T>
class Pipeline : public sc_core::sc_object
{
    struct StageDataState
    {
        T* data{nullptr};
        bool stalling{false};

        bool initialized() const { return data; }

        template<typename... Args>
        void allocate(FreeList<T>& free_list, Args&&... args)
        {
            data = free_list.allocate();
            new (data) T(std::forward<Args>(args)...);
        }

        template<typename U>
        void assign(U&& value)
        {
            *data = std::forward<U>(value);
        }

        void _clear(FreeList<T>& free_list, bool call_fifo_clear = true)
        {
            if (initialized()) {
                if (call_fifo_clear)
                    invoke_fifo_clear(*data);
                std::destroy_at(data);
                free_list.free(data);
            }

            reset_data();
            stalling = false;
        }

        void invalidate(FreeList<T>& free_list) { _clear(free_list); }
        void remove(FreeList<T>& free_list) { _clear(free_list, false /*don't call fifo_clear*/); }

        void move_to(StageDataState& next)
        {
            assert(!next.data);
            next.data = data;
            reset_data();
        }

        void bubble() { assert(!data); }

        void stall() { stalling = initialized(); }

    private:
        void reset_data() { data = nullptr; }
    };

public:
    class ConstData
    {
    public:
        explicit ConstData(const StageDataState& stage_data) : stage_data{const_cast<StageDataState&>(stage_data)} {}

        const T& get() const
        {
            if (!initialized())
                throw std::out_of_range{"get() of uninitialized pipe stage data"};
            return *stage_data.data;
        }
        operator T() const { return get(); }

        const T* operator->() const { return stage_data.data; }

        bool initialized() const { return stage_data.initialized(); }

    protected:
        StageDataState& stage_data;
    };

    class Data : public ConstData
    {
    public:
        Data(StageDataState& stage_data, FreeList<T>& free_list) : ConstData{stage_data}, free_list{free_list} {}

        T& get()
        {
            if (!this->initialized())
                throw std::out_of_range{"get() of uninitialized pipe stage data"};
            return *this->stage_data.data;
        }

        T* operator->() { return this->stage_data.data; }

        Data& operator=(const T& value)
        {
            if (this->initialized())
                this->stage_data.assign(value);
            else
                this->stage_data.allocate(free_list, value);
            return *this;
        }

        Data& operator=(T&& value)
        {
            if (this->initialized())
                this->stage_data.assign(std::move(value));
            else
                this->stage_data.allocate(free_list, std::move(value));
            return *this;
        }

        void invalidate() { this->stage_data.invalidate(free_list); }
        void remove() { this->stage_data.remove(free_list); }

    private:
        FreeList<T>& free_list;
    };

    using StageFunction = std::function<bool(Data)>;

private:
    struct Stage : StageDataState
    {
        StageFunction function{nullptr};

        Data as_data(FreeList<T>& free_list) { return Data{*this, free_list}; }
        ConstData as_const_data() const { return ConstData{*this}; }
        bool run(FreeList<T>& free_list)
        {
            bool advance = function ? (function)(as_data(free_list)) : true;
            this->stalling = !advance;
            return advance;
        }
    };

    using Stages = std::vector<Stage>;

    template<typename stage_iterator, typename U>
    class _iterator : public boost::iterator_facade<_iterator<stage_iterator, U>, U, boost::forward_traversal_tag>
    {
        stage_iterator iter;
        stage_iterator end_iter;
        void skip_till_valid()
        {
            for (; iter != end_iter && !iter->initialized(); ++iter) {}
        }

    public:
        _iterator(stage_iterator iter, stage_iterator end_iter) : iter(iter), end_iter(end_iter) { skip_till_valid(); }

    private:
        friend class boost::iterator_core_access;

        void increment()
        {
            if (iter != end_iter)
                ++iter;
            skip_till_valid();
        }

        bool equal(const _iterator& other) const { return iter == other.iter; }
        U& dereference() const { return *iter->data; }
    };

    using iterator = _iterator<typename Stages::iterator, T>;
    using const_iterator = _iterator<typename Stages::const_iterator, const T>;

    struct PipelineConfiguration : public sc_core::sc_object
    {
        const sc_core::sc_object& scope;

        Stages pipe_stages;
        bool collapse_bubbles{false};

        PipelineConfiguration(const sc_core::sc_object& scope,
                              std::initializer_list<std::pair<std::string, StageFunction>> functions);
    };

    template<typename U>
    friend class PipelineGroup;

    explicit Pipeline(const std::string& pipe_name) : sc_core::sc_object(pipe_name.c_str()) {}

    void configure(const PipelineConfiguration& config);

public:
    Pipeline(std::initializer_list<StageFunction> functions);
    Pipeline(const std::string& pipe_name, std::initializer_list<std::pair<std::string, StageFunction>> functions)
        : Pipeline(pipe_name)
    {
        configure(PipelineConfiguration(*this, functions));
    }

    Pipeline(const Pipeline&) = delete;
    ~Pipeline() override { clear(); }

    bool empty() const;
    void run();
    void clear();

    size_t size() const { return pipe_stages.size(); }
    Data operator[](size_t index);
    ConstData operator[](size_t index) const;

    // Iteration order is stage N-1..0
    iterator begin() { return iterator(pipe_stages.begin(), pipe_stages.end()); }
    iterator end() { return iterator(pipe_stages.end(), pipe_stages.end()); }

    const_iterator begin() const { return const_iterator(pipe_stages.cbegin(), pipe_stages.cend()); }
    const_iterator end() const { return const_iterator(pipe_stages.cend(), pipe_stages.cend()); }
    const_iterator cbegin() const { return const_iterator(pipe_stages.cbegin(), pipe_stages.cend()); }
    const_iterator cend() const { return const_iterator(pipe_stages.cend(), pipe_stages.cend()); }

private:
    bool collapse_bubbles{false};
    Stages pipe_stages;
    FreeList<T> free_list{1};

    bool is_first_stage(const Stage& s);

    Stage& get_stage(unsigned index) { return pipe_stages[pipe_stages.size() - index - 1]; }
    const Stage& get_stage(unsigned index) const { return const_cast<Pipeline&>(*this).get_stage(index); }

    void execute();
    Stage* execute(Stage& stage, Stage* next_stage);
    Stage* execute(unsigned index, Stage* next_stage) { return execute(get_stage(index), next_stage); }
    void advance();
};

template<typename T>
Pipeline<T>::PipelineConfiguration::PipelineConfiguration(
    const sc_core::sc_object& scope, std::initializer_list<std::pair<std::string, StageFunction>> functions)
    : scope(scope)
{
    auto* module = get_nearest_ancestor<Module>(&scope);
    AMPERE_ASSERT(module, "named Pipeline instantiated outside module");
    auto& parameters = module->get_environment()->parameters;

    size_t index = 0;

    for (auto& f : functions) {
        const auto& param = parameters.template add_parameter_with_type<size_t>(
            fmt::format("{}.{}.index", scope.name(), f.first),
            fmt::format("Pipeline {} {} stage index", scope.basename(), f.first),
            index,
            "default");

        if (!param.using_default()) {
            auto stage_index = from_json_T<size_t>(param.value());
            AMPERE_ASSERT(
                stage_index >= index, "Pipeline stage {}.{} has a non-increasing index", scope.name(), f.first);
            index = stage_index;
        }

        pipe_stages.resize(index + 1);
        pipe_stages[index].function = f.second;
        ++index;
    }

    const auto& param = parameters.template add_parameter_with_type<bool>(
        fmt::format("{}.collapse_bubbles", scope.name()),
        fmt::format("Pipeline {} should collapse bubbles", scope.basename()),
        collapse_bubbles,
        "default");

    if (!param.using_default()) {
        auto collapse_param = from_json_T<bool>(param.value());
        collapse_bubbles = collapse_param;
    }
}

template<typename T>
Pipeline<T>::Pipeline(std::initializer_list<StageFunction> functions)
    : pipe_stages(functions.size()), free_list(functions.size())
{
    size_t index = functions.size() - 1;

    for (auto& f : functions)
        pipe_stages[index--].function = f;
}

template<typename T>
void Pipeline<T>::configure(const PipelineConfiguration& config)
{
    collapse_bubbles = config.collapse_bubbles;
    pipe_stages = config.pipe_stages;
    free_list.resize(pipe_stages.size());
    std::reverse(pipe_stages.begin(), pipe_stages.end());
}

template<typename T>
bool Pipeline<T>::empty() const
{
    for (auto& s : pipe_stages)
        if (s.initialized())
            return false;
    return true;
}

template<typename T>
void Pipeline<T>::run()
{
    if (pipe_stages.empty())
        return;

    execute();
    advance();
}

template<typename T>
void Pipeline<T>::clear()
{
    for (auto& s : pipe_stages) {
        /* Note: The intention behind calling Data.invalidate() here is to
         * ensure that the logic to clear/invalidate individual stages
         * remains in sync with the logic to clear all of them. */
        s.invalidate(free_list);
    }
}

template<typename T>
typename Pipeline<T>::Data Pipeline<T>::operator[](size_t index)
{
    if (index >= pipe_stages.size())
        throw std::out_of_range{"pipe stage index out of range"};

    auto& s = get_stage(index);
    return s.as_data(free_list);
}

template<typename T>
typename Pipeline<T>::ConstData Pipeline<T>::operator[](size_t index) const
{
    if (index >= pipe_stages.size())
        throw std::out_of_range{"pipe stage index out of range"};

    auto& s = get_stage(index);
    return s.as_const_data();
}

template<typename T>
typename Pipeline<T>::Stage* Pipeline<T>::execute(Stage& stage, Stage* next_stage)
{
    bool run_stage = false;
    bool next_stage_stalling = next_stage && next_stage->stalling;
    if (next_stage_stalling) {
        // If the stage following the current stage is stalling, the
        // current should not run. If this stage has initialized data (i.e.
        // cannot be "collapsed", mark it as stalling for advance()'s later
        // use).
        stage.stall();
    } else if (is_first_stage(stage)) {
        // Special-case the first stage in the pipeline - it should always
        // run if the stage after it is not stalled and should initialize
        // the data.
        stage.stalling = false;
        run_stage = true;
    } else {
        // If the stage after the current stage is not stalled and is not
        // the first stage in the pipeline, run it if it is initialized.
        run_stage = stage.initialized();
    }

    if (run_stage) {
        stage.run(free_list);
        if (stage.stalling && !collapse_bubbles)
            return nullptr;
    }

    return &stage;
}

/*
 * Execute pipeline stages in the reverse order. Mark any stage as stalling if
 * it returns `false`. If `collapse_bubbles` is true, stall the entire pipeline
 * upstream of the first stall found (starting at the end of the pipeline).
 * Otherwise, allow stages to execute as long as there is at least one empty
 * stage between the closest stall and that upstream stage.
 */
template<typename T>
void Pipeline<T>::execute()
{
    Stage* s_next = nullptr;

    for (auto& s : pipe_stages) {
        s_next = execute(s, s_next);
        if (!s_next)
            break;
    }
}

template<typename T>
bool Pipeline<T>::is_first_stage(const Stage& s)
{
    return &s == &pipe_stages.back();
}

/*
 * Move data from each stage to the next if not stalled. If `collapse_bubbles` is
 * true, allow advancing as long as the immediately-following stage is not
 * stalling (as determined by execute(), above).
 */
template<typename T>
void Pipeline<T>::advance()
{
    Stage* s_next = nullptr;

    for (auto& s : pipe_stages) {
        if (s.stalling) {
            if (s_next && !s_next->stalling) {
                s_next->bubble();
            }
            if (!collapse_bubbles) {
                break;
            }
        } else if (s.initialized()) {
            if (s_next) {
                // We have data, are not stalled, and are not the final
                // pipeline stage, so advance our data to the next stage
                s.move_to(*s_next);
            } else {
                // This is the final pipeline stage (s_next is nullptr), so
                // since we're not stalled (checked up above), discard the
                // transaction that's leaving the pipeline.
                s.invalidate(free_list);
            }
        }

        s_next = &s;
    }
}

#define AMPERE_PIPE_STAGE_M(MOD, FUN) {#FUN, std::bind_front(&MOD::FUN, this)}

template<typename T>
class PipelineGroup : public sc_core::sc_module
{
    using Stage = typename Pipeline<T>::Stage;
    // Compare* functions should return True if the first item should be run()
    // prior to the second
    using CompareData = std::function<bool(const T&, const T&)>;
    using ComparePipeline = std::function<bool(const Pipeline<T>&, const Pipeline<T>&)>;
    typename Pipeline<T>::PipelineConfiguration config;
    Param<unsigned> _width;
    sc_core::sc_vector<Pipeline<T>> pipelines;
    bool cross_pipeline_stall{false};
    CompareData comp_data_func;
    ComparePipeline comp_pipe_func;
    std::vector<unsigned> initial_pipeline_order;

    template<typename base_iterator, typename pipeline_iterator, typename U>
    class _iterator
        : public boost::iterator_facade<_iterator<base_iterator, pipeline_iterator, U>, U, boost::forward_traversal_tag>
    {
        base_iterator iter;
        base_iterator end_iter;
        std::optional<pipeline_iterator> pipe_iter;
        std::optional<pipeline_iterator> pipe_end_iter;

    public:
        _iterator(base_iterator iter, base_iterator end_iter) : iter(iter), end_iter(end_iter)
        {
            initialize_pipeline_iterators();
            skip_till_valid();
        }

    private:
        friend class boost::iterator_core_access;

        void initialize_pipeline_iterators()
        {
            if (iter != end_iter) {
                pipe_iter = iter->begin();
                pipe_end_iter = iter->end();
            } else {
                pipe_iter = std::nullopt;
                pipe_end_iter = std::nullopt;
            }
        }

        void skip_till_valid()
        {
            while (iter != end_iter && pipe_iter == pipe_end_iter) {
                ++iter;
                initialize_pipeline_iterators();
            }
        }

        void increment()
        {
            if (pipe_iter) {
                ++*pipe_iter;
                skip_till_valid();
            }
        }

        bool equal(const _iterator& other) const { return iter == other.iter && pipe_iter == other.pipe_iter; }
        U& dereference() const { return **pipe_iter; }
    };

    void advance()
    {
        for (auto& pipeline : pipelines)
            pipeline.advance();
    }

    bool execute_stage(unsigned pipeline, unsigned stage, Stage*& next_stage, bool last_stage = false)
    {
        if (!next_stage && !last_stage)
            return false;

        next_stage = pipelines[pipeline].execute(stage, next_stage);
        return next_stage;
    }

    void execute_stages(unsigned stage,
                        const decltype(initial_pipeline_order)& order,
                        std::vector<Stage*>& next_stages,
                        bool last_stage)
    {
        bool stalled = false;
        for (unsigned pipeline : order) {
            if (stalled) {
                next_stages[pipeline] = nullptr;
                pipelines[pipeline].get_stage(stage).stall();
            } else {
                bool next = execute_stage(pipeline, stage, next_stages[pipeline], last_stage);
                stalled = !next && cross_pipeline_stall;
            }
        }
    }

public:
    /*
     * Create PipelineGroup
     *
     * cross_pipeline_stall: Force subsequent pipelines (by order) in same stage to stall if prior
     *                       ones stall. Requires compare functions to enforce explicit ordering
     *                       between pipelines (per-stage) if bubbles are collapsible.
     */
    PipelineGroup(const sc_core::sc_module_name& name,
                  std::initializer_list<std::pair<std::string, typename Pipeline<T>::StageFunction>> functions,
                  bool cross_pipeline_stall,
                  unsigned default_width = 1,
                  CompareData comp_data_func = nullptr,
                  ComparePipeline comp_pipe_func = nullptr)
        : sc_core::sc_module(name),
          config(*this, functions),
          _width("width", "Pipeline width", default_width),
          pipelines("pipeline",
                    _width,
                    [&](const char* name, int index [[maybe_unused]]) -> Pipeline<T>* {
                        auto* pipe = new Pipeline<T>(name);
                        pipe->configure(config);
                        return pipe;
                    }),
          cross_pipeline_stall(cross_pipeline_stall),
          comp_data_func(comp_data_func),
          comp_pipe_func(comp_pipe_func),
          initial_pipeline_order(width(), 0)
    {
        AMPERE_ASSERT(!cross_pipeline_stall || !config.collapse_bubbles || comp_data_func || comp_pipe_func,
                      "When cross-pipeline stalls are used with collapsing bubbles, a compare function should be "
                      "provided for ordering");

        for (unsigned i = 0; i < width(); ++i)
            initial_pipeline_order[i] = i;
    }

    PipelineGroup(const sc_core::sc_module_name& name,
                  std::initializer_list<std::pair<std::string, typename Pipeline<T>::StageFunction>> functions,
                  unsigned default_width = 1,
                  CompareData comp_data_func = nullptr,
                  ComparePipeline comp_pipe_func = nullptr)
        : PipelineGroup(name, functions, false, default_width, comp_data_func, comp_pipe_func)
    {
    }

    unsigned width() const { return _width; }

    size_t size() const { return pipelines[0].size(); }
    bool empty() const
    {
        return std::all_of(pipelines.begin(), pipelines.end(), [](const auto& p) { return p.empty(); });
    }

    void run()
    {
        auto pipe_order = initial_pipeline_order;
        if (comp_pipe_func) {
            std::sort(pipe_order.begin(), pipe_order.end(), [&](auto a, auto b) {
                return comp_pipe_func(pipelines[a], pipelines[b]);
            });
        }

        std::vector<Stage*> next_stages(width(), nullptr);
        for (unsigned s = size() - 1, i = 0; i < size(); --s, ++i) {
            if (comp_data_func && width() > 1) {
                auto sorted_order = pipe_order;
                std::sort(sorted_order.begin(), sorted_order.end(), [&](auto a, auto b) {
                    auto as = pipelines[a][s];
                    auto bs = pipelines[b][s];
                    if (as.initialized() && bs.initialized())
                        return comp_data_func(as.get(), bs.get());
                    return as.initialized() && !bs.initialized();
                });

                execute_stages(s, sorted_order, next_stages, !i);
            } else {
                execute_stages(s, pipe_order, next_stages, !i);
            }
        }

        advance();
    }

    void clear()
    {
        for (auto& pipeline : pipelines)
            pipeline.clear();
    }

    Pipeline<T>& operator[](size_t index) { return pipelines[index]; }
    const Pipeline<T>& operator[](size_t index) const { return pipelines[index]; }

    using iterator = _iterator<decltype(pipelines.begin()), typename Pipeline<T>::iterator, T>;
    using const_iterator = _iterator<decltype(pipelines.cbegin()), typename Pipeline<T>::const_iterator, const T>;

    // Iteration order is pipeline 0: stage N-1..0, ... pipeline M-1: stage N-1..0
    iterator begin() { return iterator(pipelines.begin(), pipelines.end()); }
    iterator end() { return iterator(pipelines.end(), pipelines.end()); }

    const_iterator begin() const { return const_iterator(pipelines.cbegin(), pipelines.cend()); }
    const_iterator end() const { return const_iterator(pipelines.cend(), pipelines.cend()); }
    const_iterator cbegin() const { return const_iterator(pipelines.cbegin(), pipelines.cend()); }
    const_iterator cend() const { return const_iterator(pipelines.cend(), pipelines.cend()); }
};

} // namespace ampere::sc
