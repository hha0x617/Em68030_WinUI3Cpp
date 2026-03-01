#include "pch.h"
#include <gtest/gtest.h>
#include "Helpers/MmuTestFixture.h"
#include "Core/BusErrorException.h"

using namespace Em68030::Core;

// SSW (Special Status Word) エンコーディングテスト。
// WP ページへの write → BusErrorException → SSW チェック。

class SswEncodingTests : public Em68030::Tests::MmuTestFixture {
protected:
    BusErrorException TriggerBusError(uint8_t functionCode, bool isWrite)
    {
        if (isWrite)
        {
            SetupWriteProtectedPage(0x40000000, 0x04000000);
        }
        else
        {
            SetupInvalidPage(0x40000000);
        }
        FlushAtc();

        try
        {
            Mmu.Translate(0x40000000, (functionCode & 4) != 0, isWrite, functionCode);
        }
        catch (const BusErrorException& ex)
        {
            return ex;
        }
        // Should not reach here
        ADD_FAILURE() << "Expected BusErrorException was not thrown";
        return BusErrorException(0, false, 0, 0);
    }
};

TEST_F(SswEncodingTests, BuildSSW_ReadAccess_SetsRWBit)
{
    auto ex = TriggerBusError(5, false);

    // RW bit (bit 6) should be 1 for read access
    EXPECT_NE(0, ex.SpecialStatusWord & 0x0040);
}

TEST_F(SswEncodingTests, BuildSSW_WriteAccess_ClearsRWBit)
{
    auto ex = TriggerBusError(5, true);

    // RW bit (bit 6) should be 0 for write access
    EXPECT_EQ(0, ex.SpecialStatusWord & 0x0040);
}

TEST_F(SswEncodingTests, BuildSSW_AlwaysSets_DFBit)
{
    auto exRead = TriggerBusError(5, false);
    auto exWrite = TriggerBusError(5, true);

    // DF bit (bit 8) should always be 1
    EXPECT_NE(0, exRead.SpecialStatusWord & 0x0100);
    EXPECT_NE(0, exWrite.SpecialStatusWord & 0x0100);
}

TEST_F(SswEncodingTests, BuildSSW_PreservesFunctionCode)
{
    auto ex = TriggerBusError(5, false);

    // FC bits (bits 2-0) should contain the function code
    EXPECT_EQ(5, ex.SpecialStatusWord & 0x07);
}

TEST_F(SswEncodingTests, BuildSSW_SupervisorData_FC5)
{
    auto ex = TriggerBusError(5, false);

    // FC=5, RW=1 (read), DF=1
    int fc = ex.SpecialStatusWord & 0x07;
    bool rw = (ex.SpecialStatusWord & 0x0040) != 0;
    bool df = (ex.SpecialStatusWord & 0x0100) != 0;

    EXPECT_EQ(5, fc);
    EXPECT_TRUE(rw);
    EXPECT_TRUE(df);
}

TEST_F(SswEncodingTests, BuildSSW_UserData_FC1)
{
    auto ex = TriggerBusError(1, false);

    // FC=1, RW=1 (read), DF=1
    int fc = ex.SpecialStatusWord & 0x07;
    EXPECT_EQ(1, fc);
}
