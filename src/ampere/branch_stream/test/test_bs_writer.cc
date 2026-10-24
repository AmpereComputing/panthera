//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <gtest/gtest.h>

#include "ampere/archx/sysreg.h"
#include "ampere/branch_stream/branch_stream.h"
#include "ampere/protobuf_utils/protobuf_utils.h"

#include <google/protobuf/io/zero_copy_stream_impl.h>

namespace ampere {

using namespace boost::filesystem;

class BranchStreamWriterTest : public ::testing::Test
{
protected:
    path test_file_name(const char* extension)
    {
        const ::testing::TestInfo* const test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        path p(test_info->name());
        p.replace_extension(extension);
        return p;
    }
    google::protobuf::io::FileOutputStream* output_stream;
};

TEST_F(BranchStreamWriterTest, testWrite)
{
    bs::BranchStreamWriter* writer = new bs::BranchStreamWriter(this->test_file_name("bs").string().c_str());

    is::Context context;
    context.set_type(is::Context_Type_EL);
    context.set_value(0);
    ASSERT_TRUE(writer->write(context));

    bs::Branch branch;
    branch.set_type(bs::Branch_Type_UNCONDITIONAL_INDIRECT);
    branch.set_taken(true);
    branch.set_pc(0xdeadbeef);
    branch.set_target(0xf00ba7);
    ASSERT_TRUE(writer->write(branch));

    branch.set_type(bs::Branch_Type_RETURN);
    branch.set_taken(true);
    branch.set_pc(0x7fc9bcb6108c);
    branch.set_target(0x7fc9bc980094);
    ASSERT_TRUE(writer->write(branch));

    context.set_type(is::Context_Type_EL);
    context.set_value(1);
    ASSERT_TRUE(writer->write(context));

    is::SysReg sysreg;
    sysreg.set_type(is::SysReg_Type_WRITE);
    sysreg.set_op0(archx::CFP_RCTX.op0);
    sysreg.set_op1(archx::CFP_RCTX.op1);
    sysreg.set_crn(archx::CFP_RCTX.crn);
    sysreg.set_crm(archx::CFP_RCTX.crm);
    sysreg.set_op2(archx::CFP_RCTX.op2);
    sysreg.set_value(0xdeadc0def000baa7);
    ASSERT_TRUE(writer->write(sysreg));

    branch.set_type(bs::Branch_Type_CONDITIONAL_DIRECT);
    branch.set_taken(false);
    branch.set_pc(0xfffff00ba8);
    branch.clear_target();
    ASSERT_TRUE(writer->write(branch));

    delete writer;

    int fd = open(this->test_file_name("bs").string().c_str(), O_RDONLY);
    ASSERT_GT(fd, 2);

    google::protobuf::io::FileInputStream* input_stream;
    input_stream = new google::protobuf::io::FileInputStream(fd);

    uint32_t magic, bs_version;
    readHeaderFrom(input_stream, &magic, &bs_version);
    ASSERT_EQ(magic, 0x53424250); // 0x50(P) 0x42(B) 0x42(B) 0x53(S));
    ASSERT_EQ(bs_version, 1);

    bs::Event event1;
    ASSERT_TRUE(readDelimitedFrom(input_stream, &event1, nullptr));
    ASSERT_TRUE(event1.has_context());
    ASSERT_EQ(is::Context_Type_EL, event1.context().type());
    ASSERT_EQ(0, event1.context().value());

    bs::Event event2;
    ASSERT_TRUE(readDelimitedFrom(input_stream, &event2, nullptr));
    ASSERT_TRUE(event2.has_branch());
    ASSERT_EQ(bs::Branch_Type_UNCONDITIONAL_INDIRECT, event2.branch().type());
    ASSERT_EQ(true, event2.branch().taken());
    ASSERT_EQ(0xdeadbeef, event2.branch().pc());
    ASSERT_EQ(0xf00ba7, event2.branch().target());

    bs::Event event3;
    ASSERT_TRUE(readDelimitedFrom(input_stream, &event3, nullptr));
    ASSERT_TRUE(event3.has_branch());
    ASSERT_EQ(bs::Branch_Type_RETURN, event3.branch().type());
    ASSERT_EQ(true, event3.branch().taken());
    ASSERT_EQ(0x7fc9bcb6108c, event3.branch().pc());
    ASSERT_EQ(0x7fc9bc980094, event3.branch().target());

    bs::Event event4;
    ASSERT_TRUE(readDelimitedFrom(input_stream, &event4, nullptr));
    ASSERT_TRUE(event4.has_context());
    ASSERT_EQ(is::Context_Type_EL, event4.context().type());
    ASSERT_EQ(1, event4.context().value());

    bs::Event event5;
    ASSERT_TRUE(readDelimitedFrom(input_stream, &event5, nullptr));
    ASSERT_TRUE(event5.has_sysreg());
    ASSERT_EQ(is::SysReg_Type_WRITE, event5.sysreg().type());
    ASSERT_EQ(archx::CFP_RCTX.op0, event5.sysreg().op0());
    ASSERT_EQ(archx::CFP_RCTX.op1, event5.sysreg().op1());
    ASSERT_EQ(archx::CFP_RCTX.crn, event5.sysreg().crn());
    ASSERT_EQ(archx::CFP_RCTX.crm, event5.sysreg().crm());
    ASSERT_EQ(archx::CFP_RCTX.op2, event5.sysreg().op2());
    ASSERT_EQ(0xdeadc0def000baa7, event5.sysreg().value());

    bs::Event event6;
    ASSERT_TRUE(readDelimitedFrom(input_stream, &event6, nullptr));
    ASSERT_TRUE(event6.has_branch());
    ASSERT_EQ(bs::Branch_Type_CONDITIONAL_DIRECT, event6.branch().type());
    ASSERT_EQ(false, event6.branch().taken());
    ASSERT_EQ(0xfffff00ba8, event6.branch().pc());
    ASSERT_EQ(0, event6.branch().target());

    ASSERT_TRUE(input_stream->Close());

    ASSERT_EQ(0, unlink(this->test_file_name("bs").string().c_str()));

    delete input_stream;
}

TEST_F(BranchStreamWriterTest, testWriteXz)
{
    bs::BranchStreamWriter* writer = new bs::BranchStreamWriter(this->test_file_name("bs.xz").string().c_str());

    is::Context context;
    context.set_type(is::Context_Type_EL);
    context.set_value(0);
    ASSERT_TRUE(writer->write(context));

    bs::Branch branch;
    branch.set_type(bs::Branch_Type_UNCONDITIONAL_INDIRECT);
    branch.set_taken(true);
    branch.set_pc(0xdeadbeef);
    branch.set_target(0xf00ba7);
    ASSERT_TRUE(writer->write(branch));

    branch.set_type(bs::Branch_Type_RETURN);
    branch.set_taken(true);
    branch.set_pc(0x7fc9bcb6108c);
    branch.set_target(0x7fc9bc980094);
    ASSERT_TRUE(writer->write(branch));

    context.set_type(is::Context_Type_EL);
    context.set_value(1);
    ASSERT_TRUE(writer->write(context));

    is::SysReg sysreg;
    sysreg.set_type(is::SysReg_Type_WRITE);
    sysreg.set_op0(archx::CFP_RCTX.op0);
    sysreg.set_op1(archx::CFP_RCTX.op1);
    sysreg.set_crn(archx::CFP_RCTX.crn);
    sysreg.set_crm(archx::CFP_RCTX.crm);
    sysreg.set_op2(archx::CFP_RCTX.op2);
    sysreg.set_value(0xdeadc0def000baa7);
    ASSERT_TRUE(writer->write(sysreg));

    branch.set_type(bs::Branch_Type_CONDITIONAL_DIRECT);
    branch.set_taken(false);
    branch.set_pc(0xfffff00ba8);
    branch.clear_target();
    ASSERT_TRUE(writer->write(branch));

    delete writer;

    bs::BranchStreamReader* reader = new bs::BranchStreamReader(test_file_name("bs.xz"));

    ASSERT_EQ(reader->version(), 1);

    bs::Event event1;
    ASSERT_TRUE(reader->read(event1));
    ASSERT_TRUE(event1.has_context());
    ASSERT_EQ(is::Context_Type_EL, event1.context().type());
    ASSERT_EQ(0, event1.context().value());

    bs::Branch branch1;
    ASSERT_TRUE(reader->read(branch1));
    ASSERT_EQ(bs::Branch_Type_UNCONDITIONAL_INDIRECT, branch1.type());
    ASSERT_EQ(true, branch1.taken());
    ASSERT_EQ(0xdeadbeef, branch1.pc());
    ASSERT_EQ(0xf00ba7, branch1.target());

    bs::Branch branch2;
    ASSERT_TRUE(reader->read(branch2));
    ASSERT_EQ(bs::Branch_Type_RETURN, branch2.type());
    ASSERT_EQ(true, branch2.taken());
    ASSERT_EQ(0x7fc9bcb6108c, branch2.pc());
    ASSERT_EQ(0x7fc9bc980094, branch2.target());

    bs::Event event2;
    ASSERT_TRUE(reader->read(event2));
    ASSERT_TRUE(event2.has_context());
    ASSERT_EQ(is::Context_Type_EL, event2.context().type());
    ASSERT_EQ(1, event2.context().value());

    bs::Event event3;
    ASSERT_TRUE(reader->read(event3));
    ASSERT_TRUE(event3.has_sysreg());
    ASSERT_EQ(is::SysReg_Type_WRITE, event3.sysreg().type());
    ASSERT_EQ(archx::CFP_RCTX.op0, event3.sysreg().op0());
    ASSERT_EQ(archx::CFP_RCTX.op1, event3.sysreg().op1());
    ASSERT_EQ(archx::CFP_RCTX.crn, event3.sysreg().crn());
    ASSERT_EQ(archx::CFP_RCTX.crm, event3.sysreg().crm());
    ASSERT_EQ(archx::CFP_RCTX.op2, event3.sysreg().op2());
    ASSERT_EQ(0xdeadc0def000baa7, event3.sysreg().value());

    bs::Event event4;
    ASSERT_TRUE(reader->read(event4));
    ASSERT_TRUE(event4.has_branch());
    ASSERT_EQ(bs::Branch_Type_CONDITIONAL_DIRECT, event4.branch().type());
    ASSERT_EQ(false, event4.branch().taken());
    ASSERT_EQ(0xfffff00ba8, event4.branch().pc());
    ASSERT_EQ(0, event4.branch().target());

    delete reader;

    ASSERT_EQ(0, unlink(this->test_file_name("bs.xz").string().c_str()));
}

} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
