//
// Copyright(c) 2022 Ampere Computing LLC
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <map>
#include <string>
#include <vector>

#include <fmt/format.h>

namespace ampere::evp {

template<typename ValueType,
         typename ColumnType = std::vector<ValueType>,
         typename ColumnNameType = std::string,
         typename ColumnSorter = std::less<std::string>>
struct FrameTraits
{
    using value_type = ValueType;
    using column_type = ColumnType;
    using column_name_type = ColumnNameType;
    using column_sorter = ColumnSorter;
    using table_type = std::map<column_name_type, column_type, column_sorter>;
    using row_series_type = std::map<column_name_type, value_type>;
    using col_series_type = std::pair<std::string, column_type>;
};

template<typename Value, typename Traits = FrameTraits<Value>>
class RowSeries : public Traits::row_series_type
{
    using row_series_type = typename Traits::row_series_type;

public:
    RowSeries() = default;
    RowSeries(std::initializer_list<typename row_series_type::value_type> data) : row_series_type{data} {}
};

template<typename Value, typename Traits = FrameTraits<Value>>
class ColSeries : public Traits::col_series_type
{
    using col_series_type = typename Traits::col_series_type;

    using column_type = typename Traits::column_type;
    using value_type = typename Traits::value_type;

public:
    ColSeries() = default;
    explicit ColSeries(const std::string& name) : col_series_type{name, {}} {}
    ColSeries(const std::string& name, std::initializer_list<Value> data) : col_series_type{name, data} {}
    ColSeries(const std::string& name, const column_type& data) : col_series_type{name, data} {}

    size_t size() const { return this->second.size(); }

    value_type& operator[](size_t i) { return this->second[i]; }
    const value_type& operator[](size_t i) const { return this->second[i]; }

    void push_back(const value_type& v) { this->second.push_back(v); }
};

template<typename Value, typename Traits = FrameTraits<Value>>
class Frame
{
public:
    using value_type = typename Traits::value_type;
    using column_type = typename Traits::column_type;
    using column_name_type = typename Traits::column_name_type;
    using table_type = typename Traits::table_type;

    Frame() = default;
    Frame(std::initializer_list<RowSeries<Value, Traits>>);
    Frame(std::initializer_list<ColSeries<Value, Traits>>);

private:
    class __row_view;
    class __const_row_view;

public:
    class row_iterator
    {
    public:
        using iterator_category = std::forward_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = Frame::value_type;
        using pointer = value_type*;
        using reference = value_type&;

        row_iterator(Frame& frame, size_t row) : tab_iter{frame.m_table.begin()}, row{row} {}

        reference operator*() const { return tab_iter->second.at(row); }
        pointer operator->() { return &tab_iter->second.at(row); }

        row_iterator& operator++()
        {
            tab_iter++;
            return *this;
        }

        row_iterator operator++(int)
        {
            row_iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        friend bool operator==(const row_iterator& a, const row_iterator& b)
        {
            return a.tab_iter == b.tab_iter && a.row == b.row;
        };

        friend bool operator!=(const row_iterator& a, const row_iterator& b) { return !(a == b); };

    private:
        typename table_type::iterator tab_iter;
        size_t row;

        //
        // This private constructor gives us the value for "end" iterators
        //

        explicit row_iterator(Frame& frame, size_t row, bool) : tab_iter{frame.m_table.end()}, row{row} {}

        friend class __row_view;
    };

    class const_row_iterator
    {
    public:
        using iterator_category = std::forward_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = Frame::value_type;
        using pointer = const value_type*;
        using reference = const value_type&;

        const_row_iterator(const Frame& frame, size_t row) : tab_iter{frame.m_table.cbegin()}, row{row} {}

        reference operator*() const { return tab_iter->second.at(row); }
        pointer operator->() { return &tab_iter->second.at(row); }

        const_row_iterator& operator++()
        {
            tab_iter++;
            return *this;
        }

        const_row_iterator operator++(int)
        {
            const_row_iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        friend bool operator==(const const_row_iterator& a, const const_row_iterator& b)
        {
            return a.tab_iter == b.tab_iter && a.row == b.row;
        };

        friend bool operator!=(const const_row_iterator& a, const const_row_iterator& b) { return !(a == b); };

    private:
        typename table_type::const_iterator tab_iter;
        size_t row;

        //
        // This private constructor gives us the value for "end" iterators
        //

        explicit const_row_iterator(const Frame& frame, size_t row, bool) : tab_iter{frame.m_table.cend()}, row{row} {}

        friend class __const_row_view;
    };

private:
    class __row_view
    {
    public:
        __row_view(Frame& frame, size_t row) : frame{frame}, row{row} {}

        row_iterator begin() { return row_iterator(frame, row); }
        row_iterator end() { return row_iterator(frame, row, true); }

        const_row_iterator begin() const { return const_row_iterator(frame, row); }
        const_row_iterator end() const { return const_row_iterator(frame, row, true); }

        Value& operator[](size_t c) { return frame.at(c, row); }
        const Value& operator[](size_t c) const { return frame.at(c, row); }

    private:
        Frame& frame;
        size_t row;
    };

    class __const_row_view
    {
    public:
        __const_row_view(const Frame& frame, size_t row) : frame{frame}, row{row} {}

        const_row_iterator begin() const { return const_row_iterator(frame, row); }
        const_row_iterator end() const { return const_row_iterator(frame, row, true); }

        const Value& operator[](size_t c) const { return frame.at(c, row); }

    private:
        const Frame& frame;
        size_t row;
    };

public:
    [[nodiscard]] size_t rows() const { return m_rows; };
    [[nodiscard]] size_t cols() const { return m_cols; };

    __row_view row(size_t r) { return __row_view(*this, r); }
    column_type& col(size_t c) { return *m_index.at(c); }

    __const_row_view row(size_t r) const { return __const_row_view(*this, r); }
    const column_type& col(size_t c) const { return *m_index.at(c); }

    Value& at(size_t r, size_t c) { return m_index.at(c)->at(r); }
    const Value& at(size_t r, size_t c) const { return m_index.at(c)->at(r); }

    __row_view operator[](size_t r) { return __row_view(*this, r); }
    __const_row_view operator[](size_t r) const { return __const_row_view(*this, r); }

    column_type& operator[](const std::string& col_name) { return m_table.at(col_name); }
    const column_type& operator[](const std::string& col_name) const { return m_table.at(col_name); }

    void add_row(const RowSeries<value_type, Traits>&);
    void add_col(const ColSeries<value_type, Traits>&);

    void rename(const column_name_type& old_col_name, const column_name_type& new_col_name);

    std::vector<column_name_type> columns() const;

    auto begin() { return m_table.begin(); }
    auto end() { return m_table.end(); }
    auto begin() const { return m_table.cbegin(); }
    auto end() const { return m_table.cend(); }

private:
    table_type m_table;
    size_t m_rows{0};
    size_t m_cols{0};

    std::vector<column_type*> m_index;

    void reindex();
};

template<typename Value, typename Traits>
Frame<Value, Traits>::Frame(std::initializer_list<RowSeries<Value, Traits>> rows)
{
    for (auto& row : rows)
        add_row(row);
}

template<typename Value, typename Traits>
Frame<Value, Traits>::Frame(std::initializer_list<ColSeries<Value, Traits>> cols)
{
    for (auto& col : cols)
        add_col(col);
}

template<typename Value, typename Traits>
void Frame<Value, Traits>::add_row(const RowSeries<value_type, Traits>& series)
{
    for (auto& [name, value] : series) {
        auto [col, added] = m_table.try_emplace(name, column_type(m_rows));
        if (added)
            ++m_cols;

        col->second.emplace_back(value);
    }

    ++m_rows;

    for (auto& entry : m_table) {
        auto& col = entry.second;

        if (col.size() < m_rows)
            col.emplace_back();
    }

    reindex();
}

template<typename Value, typename Traits>
void Frame<Value, Traits>::add_col(const ColSeries<value_type, Traits>& series)
{
    auto& [name, data] = series;

    if (m_rows == 0)
        m_rows = data.size();
    else if (data.size() != m_rows)
        throw std::runtime_error{"Frame requires columns of equal size"};

    if (m_table.count(name))
        throw std::runtime_error{fmt::format("duplicate column name \"{}\"", name)};

    m_table[name] = data;
    ++m_cols;

    reindex();
}

template<typename Value, typename Traits>
void Frame<Value, Traits>::reindex()
{
    m_index.clear();

    for (auto& entry : m_table)
        m_index.push_back(&entry.second);
}

template<typename Value, typename Traits>
void Frame<Value, Traits>::rename(const column_name_type& old_col_name, const column_name_type& new_col_name)
{
    auto it = m_table.find(old_col_name);

    if (it == m_table.end())
        throw std::runtime_error{fmt::format("no such column \"{}\"", old_col_name)};

    if (m_table.count(new_col_name))
        throw std::runtime_error{fmt::format("duplicate column name \"{}\"", new_col_name)};

    m_table[new_col_name] = std::move(it->second);
    m_table.erase(it);

    reindex();
}

template<typename Value, typename Traits>
auto Frame<Value, Traits>::columns() const -> std::vector<column_name_type>
{
    std::vector<column_name_type> column_names;
    column_names.reserve(m_table.size());

    for (const auto& col : m_table)
        column_names.emplace_back(col.first);

    return column_names;
}

} // namespace ampere::evp
