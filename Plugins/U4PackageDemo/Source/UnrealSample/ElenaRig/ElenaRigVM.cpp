#include "ElenaRigVM.h"
#include "ElenaRigMath.h"

#include <algorithm>
#include <cmath>
#include <immintrin.h>

namespace ElenaRig
{
Program ReadProgram(std::span<const std::byte> Data)
{
    std::uint32_t Offset = 0;
    auto ReadWord = [Data, &Offset]()
    {
        const auto Value = Parameter<std::uint32_t>(Data, Offset);
        Offset += 4;
        return Value;
    };
    const auto Bones = ReadWord(), Groups = ReadWord(), Blocks = ReadWord();
    Program Result;
    for (unsigned I = 0; I != Groups; ++I)
    {
        Group G;
        G.FirstBone = ReadWord(); G.BoneCount = ReadWord();
        const auto Count = ReadWord();
        for (unsigned J = 0; J != Count; ++J) G.Blocks.push_back(ReadWord());
        Result.Groups.push_back(std::move(G));
    }
    for (unsigned I = 0; I != Blocks; ++I)
    {
        const auto Size = ReadWord();
        Result.Blocks.emplace_back(Data.begin() + Offset, Data.begin() + Offset + Size);
        Offset += Size;
    }
    Result.ReferencePose.resize(Bones);
    std::memcpy(Result.ReferencePose.data(), Data.data() + Offset, Bones * sizeof(SQT));
    return Result;
}

static Vector MultiplyQuaternion(const Vector& A, const Vector& B)
{
    return {((B[2] * A[1] + A[0] * B[3]) + B[0] * A[3]) - A[2] * B[1],
            ((A[2] * B[0] + A[1] * B[3]) + B[1] * A[3]) - B[2] * A[0],
            ((B[1] * A[0] + A[2] * B[3]) + B[2] * A[3]) - A[1] * B[0],
            ((A[3] * B[3] - A[0] * B[0]) - A[1] * B[1]) - A[2] * B[2]};
}

static float SourceReciprocal(float Value)
{
    const float R = _mm_cvtss_f32(_mm_rcp_ss(_mm_set_ss(Value)));
    return (R + R) - (R * R) * Value;
}

static Vector Cross(const Vector& A, const Vector& B)
{
    return {A[1]*B[2]-A[2]*B[1], A[2]*B[0]-A[0]*B[2], A[0]*B[1]-A[1]*B[0], 0};
}

static Vector NormalizeQuaternion(Vector Q)
{
    const float Length = ((Q[1]*Q[1] + Q[0]*Q[0]) + Q[2]*Q[2]) + Q[3]*Q[3];
    float R = _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(Length)));
    R = (R * 0.5f) * (3.0f - (R * Length) * R);
    R = (R * 0.5f) * (3.0f - (R * Length) * R);
    for (float& Value : Q) Value *= R;
    return Q;
}

static Matrix InverseColumns(const Matrix& Input)
{
    Matrix Columns{};
    for (unsigned Column = 0; Column != 3; ++Column)
        for (unsigned Row = 0; Row != 3; ++Row) Columns[Column][Row] = Input[Row][Column] * Input[3][Column];
    const auto C01 = Cross(Columns[0], Columns[1]);
    const auto C12 = Cross(Columns[1], Columns[2]);
    const auto C20 = Cross(Columns[2], Columns[0]);
    const float Determinant = C01[2] * Columns[2][2] + (C01[0] * Columns[2][0] + C01[1] * Columns[2][1]);
    const float Reciprocal = SourceReciprocal(Determinant);
    Matrix Result{};
    for (unsigned Axis = 0; Axis != 3; ++Axis)
    {
        Columns[Axis] = {C12[Axis], C20[Axis], C01[Axis], 0};
        for (unsigned Row = 0; Row != 3; ++Row) Result[Axis][Row] = Reciprocal * Columns[Axis][Row];
    }
    for (unsigned Row = 0; Row != 3; ++Row)
        Result[3][Row] = (((0.0f - Input[1][3]) * Columns[1][Row] + (0.0f - Input[0][3]) * Columns[0][Row])
            + (0.0f - Input[2][3]) * Columns[2][Row]) * Reciprocal + 0.0f;
    Result[3][3] = 1;
    return Result;
}

void VM::AccumulatePoint(std::span<const std::byte> Block, std::uint32_t Count, std::uint32_t Table, bool ParentPosition)
{
    const auto Scalars = Table + Count * 4;
    for (unsigned I = 0; I < (Count + 3) / 4 * 4; ++I)
    {
        const auto Source = Parameter<std::uint16_t>(Block, Table + I * 4);
        if (Source != 0) // Native padding sentinel.
        {
            const auto M = Read<Matrix>(Source, 16);
            Vector Position{M[0][3], M[1][3], M[2][3], 1};
            const auto ScalarRecord = Scalars + I * (ParentPosition ? 8 : 2);
            if (ParentPosition)
            {
                Vector Local;
                for (unsigned Axis = 0; Axis != 3; ++Axis)
                    Local[Axis] = Read<float>(Parameter<std::uint16_t>(Block, ScalarRecord + Axis * 2)) * M[3][Axis];
                for (unsigned Axis = 0; Axis != 3; ++Axis)
                    Position[Axis] = (Local[0] * M[Axis][0] + Local[1] * M[Axis][1]) + (Local[2] * M[Axis][2] + M[Axis][3]);
            }
            const float Weight = Read<float>(Parameter<std::uint16_t>(Block, ScalarRecord + (ParentPosition ? 6 : 0)));
            const auto Destination = Parameter<std::uint16_t>(Block, Table + I * 4 + 2);
            for (auto& Value : Position) Value *= Weight;
            if (!(Source & 8))
            {
                const auto Previous = Read<Vector>(Destination, 16);
                for (unsigned Axis = 0; Axis != 4; ++Axis) Position[Axis] += Previous[Axis];
            }
            Write(Destination, Position, 16);
        }
    }
}

void VM::NormalizePoint(std::span<const std::byte> Block, std::uint32_t Count, std::uint32_t Table, bool AddOffset)
{
    const auto Offsets = Table + Count * 2;
    for (unsigned Batch = 0; Batch < Count; Batch += 8)
    {
        std::array<Vector, 8> Values;
        std::array<std::uint16_t, 8> Destinations;
        for (unsigned Lane = 0; Lane != 8; ++Lane)
        {
            Destinations[Lane] = Parameter<std::uint16_t>(Block, Table + (Batch + Lane) * 2);
            Values[Lane] = Read<Vector>(Destinations[Lane], 16);
            const float Reciprocal = SourceReciprocal(Values[Lane][3]);
            for (auto& Value : Values[Lane]) Value *= Reciprocal;
            if (AddOffset)
            {
                for (unsigned Axis = 0; Axis != 3; ++Axis)
                    Values[Lane][Axis] += Read<float>(Parameter<std::uint16_t>(Block, Offsets + (Batch + Lane) * 6 + Axis * 2));
                Values[Lane][3] += 0.0f;
            }
        }
        for (unsigned Lane = 0; Lane != 8; ++Lane) Write(Destinations[Lane], Values[Lane], 16);
    }
}

void VM::LocalizePoint(std::span<const std::byte> Block, std::uint32_t Count, std::uint32_t Table, bool AddOffset)
{
    const auto Offsets = Table + Count * 4;
    for (unsigned Batch = 0; Batch < Count; Batch += 4)
    {
        std::array<Matrix, 4> Inverses;
        std::array<Vector, 4> Values;
        std::array<std::uint16_t, 4> Destinations;
        for (unsigned Lane = 0; Lane != 4; ++Lane)
            Inverses[Lane] = InverseColumns(Read<Matrix>(Parameter<std::uint16_t>(Block, Table + (Batch + Lane) * 4), 16));
        for (unsigned Lane = 0; Lane != 4; ++Lane)
        {
            Destinations[Lane] = Parameter<std::uint16_t>(Block, Table + (Batch + Lane) * 4 + 2);
            auto Position = Read<Vector>(Destinations[Lane], 16);
            const float Reciprocal = SourceReciprocal(Position[3]);
            for (auto& Value : Position) Value *= Reciprocal;
            const auto& Inverse = Inverses[Lane];
            for (unsigned Axis = 0; Axis != 4; ++Axis)
            {
                Values[Lane][Axis] = ((Position[1] * Inverse[1][Axis] + Position[0] * Inverse[0][Axis])
                    + Position[2] * Inverse[2][Axis]) + Inverse[3][Axis];
                if (AddOffset)
                    Values[Lane][Axis] += Axis == 3 ? 0.0f : Read<float>(Parameter<std::uint16_t>(Block, Offsets + (Batch + Lane) * 6 + Axis * 2));
            }
        }
        for (unsigned Lane = 0; Lane != 4; ++Lane) Write(Destinations[Lane], Values[Lane], 16);
    }
}

VM::VM()
{
    for (auto& Bank : Banks)
    {
        Bank.resize(65536);
    }
}

void VM::Evaluate(const Program& ProgramData, std::span<const SQT> Input)
{
    std::fill(Banks[3].begin(), Banks[3].end(), std::byte{});
    Worlds.resize(Input.size());
    Locals.resize(Input.size());
    const Matrix Identity{{{1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {1,1,1,1}}};
    for (const Group& GroupData : ProgramData.Groups)
    {
        for (std::uint32_t Bank = 0; Bank != 3; ++Bank)
        {
            std::fill(Banks[Bank].begin(), Banks[Bank].end(), std::byte{});
        }
        for (std::uint32_t Bone = 0; Bone != GroupData.BoneCount; ++Bone)
        {
            Write(Bone * 48, Input[GroupData.FirstBone + Bone]);
        }
        Write(1, Identity);
        for (std::uint32_t BlockIndex : GroupData.Blocks)
        {
            const auto& Block = ProgramData.Blocks[BlockIndex];
            std::uint32_t Offset = 0;
            while (Parameter<std::uint8_t>(Block, Offset) != 0)
            {
                Execute(Block, Offset);
                Offset += 2 + 2 * Parameter<std::uint8_t>(Block, Offset + 1);
            }
        }
        for (std::uint32_t Bone = 0; Bone != GroupData.BoneCount; ++Bone)
        {
            Worlds[GroupData.FirstBone + Bone] = Read<Matrix>(1 + (Bone + 1) * 64);
            Locals[GroupData.FirstBone + Bone] = Read<SQT>(Bone * 48);
        }
    }
}

void VM::Execute(std::span<const std::byte> Block, std::uint32_t CommandOffset)
{
    const auto Opcode = Parameter<std::uint8_t>(Block, CommandOffset);
    const auto Count = Parameter<std::uint16_t>(Block, CommandOffset + 2);
    const auto Param = Parameter<std::uint16_t>(Block, CommandOffset + 6);
    switch (Opcode)
    {
    case 40: // HierarchyParenting, u4 RVA 0x204ab20.
    {
        for (std::uint32_t I = 0; I != Count * 4u; ++I)
        {
            const auto Slot = Parameter<std::uint16_t>(Block, Param + I * 4);
            const auto ParentAddress = Parameter<std::uint16_t>(Block, Param + I * 4 + 2);
            const auto Local = Read<SQT>(Slot * 3 - 48);
            const auto Parent = Read<Matrix>(ParentAddress, 16);
            const Vector Scale = (ParentAddress & 8) ? Vector{1,1,1,1} : Parent[3];
            const auto& Q = Local[1];
            Vector Diagonal, Plus, Minus;
            for (std::uint32_t Axis = 0; Axis != 3; ++Axis)
            {
                const auto Next = (Axis + 1) % 3;
                const auto Last = (Axis + 2) % 3;
                Diagonal[Axis] = (1.0f - (Q[Next] + Q[Next]) * Q[Next]) - (Q[Last] + Q[Last]) * Q[Last];
                const float Product = (Q[Axis] + Q[Axis]) * Q[Last];
                const float Twist = Q[3] * (Q[Next] + Q[Next]);
                Plus[Axis] = Twist + Product;
                Minus[Axis] = Product - Twist;
            }
            Matrix Basis{{{Diagonal[0], Minus[1], Plus[0], 0},
                          {Plus[1], Diagonal[1], Minus[2], 0},
                          {Minus[0], Plus[2], Diagonal[2], 0}, Local[0]}};
            for (std::uint32_t Row = 0; Row != 3; ++Row)
            {
                for (std::uint32_t Column = 0; Column != 3; ++Column)
                {
                    Basis[Row][Column] *= Scale[Row];
                }
                Basis[Row][3] = Parent[3][Row] * Local[2][Row];
            }
            for (std::uint32_t Row = 0; Row != 3; ++Row)
            {
                Vector Result;
                for (std::uint32_t Column = 0; Column != 4; ++Column)
                {
                    const float Translation = Column == 3 ? Parent[Row][3] : 0.0f;
                    Result[Column] = ((Parent[Row][0] * Basis[0][Column] + Translation)
                        + Parent[Row][1] * Basis[1][Column]) + Parent[Row][2] * Basis[2][Column];
                }
                Write(1 + Slot * 4 + Row * 16, Result);
            }
            Write(1 + Slot * 4 + 48, Local[0]);
        }
        break;
    }
    case 41: // SDK rotation drivers: inverse pre-rotation * input * inverse post-rotation.
    case 44: // SDK driven rotations: pre-rotation * Euler result * post-rotation.
    {
        const auto Constants = Param + Count * 16;
        for (std::uint32_t Batch = 0; Batch < Count; Batch += 4)
        {
            std::array<std::uint32_t, 4> Orders;
            std::array<Vector, 3> Euler{};
            std::array<Vector, 4> Quaternion{};
            for (std::uint32_t Lane = 0; Lane != 4; ++Lane)
            {
                const auto Record = Param + (Batch + Lane) * 16;
                Orders[Lane] = Parameter<std::uint16_t>(Block, Record);
                if (Opcode == 41)
                {
                    auto Pre = Parameter<Vector>(Block, Constants + Parameter<std::uint16_t>(Block, Record + 4));
                    auto Post = Parameter<Vector>(Block, Constants + Parameter<std::uint16_t>(Block, Record + 6));
                    for (std::uint32_t Axis = 0; Axis != 3; ++Axis)
                    {
                        Pre[Axis] = -Pre[Axis];
                        Post[Axis] = -Post[Axis];
                    }
                    const auto Input = Read<Vector>(Parameter<std::uint16_t>(Block, Record + 8), 16);
                    const auto Result = MultiplyQuaternion(MultiplyQuaternion(Pre, Input), Post);
                    for (std::uint32_t Axis = 0; Axis != 4; ++Axis) Quaternion[Axis][Lane] = Result[Axis];
                }
                else
                {
                    for (std::uint32_t Axis = 0; Axis != 3; ++Axis)
                        Euler[Axis][Lane] = Read<float>(Parameter<std::uint16_t>(Block, Record + 10 + Axis * 2));
                }
            }
            auto EulerBytes = std::as_writable_bytes(std::span(Euler));
            auto QuaternionBytes = std::as_writable_bytes(std::span(Quaternion));
            auto OrderBytes = std::as_writable_bytes(std::span(Orders));
            if (Opcode == 41) SourceQuaternionToEuler(EulerBytes, QuaternionBytes, OrderBytes);
            else SourceEulerToQuaternion(QuaternionBytes, EulerBytes, OrderBytes);
            std::array<Vector, 4> Results;
            if (Opcode == 44)
            {
                for (std::uint32_t Lane = 0; Lane != 4; ++Lane)
                {
                    const auto Record = Param + (Batch + Lane) * 16;
                    const auto Pre = Parameter<Vector>(Block, Constants + Parameter<std::uint16_t>(Block, Record + 4));
                    const auto Post = Parameter<Vector>(Block, Constants + Parameter<std::uint16_t>(Block, Record + 6));
                    const Vector Q{Quaternion[0][Lane], Quaternion[1][Lane], Quaternion[2][Lane], Quaternion[3][Lane]};
                    Results[Lane] = MultiplyQuaternion(MultiplyQuaternion(Pre, Q), Post);
                }
            }
            for (std::uint32_t Lane = 0; Lane != 4; ++Lane)
            {
                const auto Record = Param + (Batch + Lane) * 16;
                if (Opcode == 41)
                {
                    for (std::uint32_t Axis = 0; Axis != 3; ++Axis)
                    {
                        const auto Destination = Parameter<std::uint16_t>(Block, Record + 10 + Axis * 2);
                        if (Destination != 0xffff) Write(Destination, Euler[Axis][Lane]);
                    }
                }
                else Write(Parameter<std::uint16_t>(Block, Record + 8), Results[Lane], 16);
            }
        }
        break;
    }
    case 47: // SDK piecewise cubic, 0x204d630 and specialized 0x2053680..0x205422f.
    {
        const auto Segments = Parameter<std::uint16_t>(Block, CommandOffset + 8);
        const auto Lower = Param + Count * 4;
        const auto Upper = Lower + Count * 4;
        const auto Thresholds = Upper + Count * 4;
        const auto Coefficients = Thresholds + (Segments - 1) * Count * 4;
        for (std::uint32_t Batch = 0; Batch != Count / 4u; ++Batch)
        {
            std::array<std::uint16_t, 4> Destinations;
            Vector Values;
            for (std::uint32_t Lane = 0; Lane != 4; ++Lane)
            {
                const auto Channel = Batch * 4 + Lane;
                const auto Source = Parameter<std::uint16_t>(Block, Param + Channel * 4);
                Destinations[Lane] = Parameter<std::uint16_t>(Block, Param + Channel * 4 + 2);
                float X = Read<float>(Source);
                float Base = Parameter<float>(Block, Lower + Channel * 4);
                const float Limit = Parameter<float>(Block, Upper + Channel * 4);
                X = X > Base ? X : Base;
                X = X < Limit ? X : Limit;
                std::uint32_t Segment = 0;
                for (std::uint32_t I = 1; I < Segments; ++I)
                {
                    const float Threshold = Parameter<float>(Block, Thresholds + ((I - 1) * Count + Channel) * 4);
                    if (Threshold < X)
                    {
                        Base = Threshold;
                        Segment = I;
                    }
                }
                const auto Address = Coefficients + Batch * Segments * 64 + Segment * 16 + Lane * 4;
                const float A = Parameter<float>(Block, Address);
                const float B = Parameter<float>(Block, Address + Segments * 16);
                const float C = Parameter<float>(Block, Address + Segments * 32);
                const float D = Parameter<float>(Block, Address + Segments * 48);
                const float T = X - Base;
                Values[Lane] = ((A * T + B) * T + C) * T + D;
                Values[Lane] += Read<float>(Destinations[Lane]);
            }
            for (std::uint32_t Lane = 0; Lane != 4; ++Lane)
            {
                Write(Destinations[Lane], Values[Lane]);
            }
        }
        break;
    }
    case 48: // Point constraint, 0x2055330.
    case 49: // Parent-position constraint, 0x20553f0.
    {
        const bool ParentPosition = Opcode == 49;
        const auto Header = ParentPosition ? Param + Count * 12 : Param + Count * 4 + ((Count + 7) & ~7u) * 2;
        const auto NormalizeCount = Parameter<std::uint32_t>(Block, Header);
        const auto LocalizeOffset = Parameter<std::uint32_t>(Block, Header + 4);
        const auto LocalizeCount = Parameter<std::uint32_t>(Block, Header + 8);
        AccumulatePoint(Block, Count, Param, ParentPosition);
        NormalizePoint(Block, NormalizeCount, Header + 16, !ParentPosition);
        LocalizePoint(Block, LocalizeCount, Header + LocalizeOffset, !ParentPosition);
        break;
    }
    case 50: // Rotation constraint, u4 RVA 0x20554b0.
    {
        const auto Offsets = Param + Count * 8;
        const auto Weights = Offsets + Count * 16;
        for (unsigned Batch = 0; Batch < Count; Batch += 4)
        {
            std::array<Vector, 12> Source{}, Parent{}, Inverse{}, Local{};
            std::array<Vector, 9> Basis{};
            std::array<Vector, 4> Quaternion{};
            for (unsigned Lane = 0; Lane != 4; ++Lane)
            {
                const auto Record = Param + (Batch + Lane) * 8;
                const auto SourceMatrix = Read<Matrix>(Parameter<std::uint16_t>(Block, Record), 16);
                const auto ParentMatrix = Read<Matrix>(Parameter<std::uint16_t>(Block, Record + 2), 16);
                for (unsigned Column = 0; Column != 4; ++Column)
                for (unsigned Row = 0; Row != 3; ++Row)
                {
                    Source[Column * 3 + Row][Lane] = SourceMatrix[Row][Column];
                    Parent[Column * 3 + Row][Lane] = Column < 3 ? ParentMatrix[3][Column] * ParentMatrix[Row][Column] : ParentMatrix[Row][Column];
                }
            }
            auto InverseBytes = std::as_writable_bytes(std::span(Inverse));
            auto LocalBytes = std::as_writable_bytes(std::span(Local));
            auto BasisBytes = std::as_writable_bytes(std::span(Basis));
            SourceMatrixInverse(InverseBytes, std::as_writable_bytes(std::span(Parent)), {});
            SourceMatrixMultiply(InverseBytes, LocalBytes, std::as_writable_bytes(std::span(Source)));
            SourceOrthonormalize(BasisBytes, LocalBytes, {});
            SourceMatrixToQuaternion(std::as_writable_bytes(std::span(Quaternion)), BasisBytes, {});
            std::array<Vector, 4> Results;
            std::array<std::uint16_t, 4> Destinations;
            for (unsigned Lane = 0; Lane != 4; ++Lane)
            {
                const auto Record = Param + (Batch + Lane) * 8;
                Vector Q{Quaternion[0][Lane],Quaternion[1][Lane],Quaternion[2][Lane],Quaternion[3][Lane]};
                Q = NormalizeQuaternion(Q);
                Vector Offset;
                for (unsigned Axis = 0; Axis != 4; ++Axis)
                    Offset[Axis] = Parameter<float>(Block, Offsets + Batch * 16 + Axis * 16 + Lane * 4);
                const auto Driver = Parameter<std::uint16_t>(Block, Record + 4);
                const auto DriverQuaternion = Read<Vector>(Driver, 16);
                Q = MultiplyQuaternion(Q, MultiplyQuaternion(DriverQuaternion, Offset));
                const float Weight = Read<float>(Parameter<std::uint32_t>(Block, Weights + (Batch + Lane) * 4));
                Destinations[Lane] = Parameter<std::uint16_t>(Block, Record + 6);
                const auto Previous = Driver & 8 ? Vector{} : Read<Vector>(Destinations[Lane], 16);
                const float Dot = ((Previous[1] * Q[1] + Q[0] * Previous[0]) + Previous[2] * Q[2]) + Previous[3] * Q[3];
                const float Sign = std::bit_cast<float>((std::bit_cast<std::uint32_t>(Dot) & 0x80000000u) | 0x3f800000u);
                for (unsigned Axis = 0; Axis != 4; ++Axis) Results[Lane][Axis] = Previous[Axis] * Sign + Weight * Q[Axis];
            }
            for (unsigned Lane = 0; Lane != 4; ++Lane) Write(Destinations[Lane], Results[Lane], 16);
        }
        const auto FinalizeCount = Parameter<std::uint32_t>(Block, Weights + Count * 4);
        const auto FinalizeTable = Weights + Count * 4 + 16;
        const auto FinalizeOffsets = FinalizeTable + ((FinalizeCount + 1) & ~1u) * 8;
        for (unsigned Batch = 0; Batch != FinalizeCount; ++Batch)
        {
            std::array<Vector, 4> Results;
            std::array<std::uint16_t, 4> Destinations;
            for (unsigned Lane = 0; Lane != 4; ++Lane)
            {
                Destinations[Lane] = Parameter<std::uint16_t>(Block, FinalizeTable + Batch * 8 + Lane * 2);
                auto Q = NormalizeQuaternion(Read<Vector>(Destinations[Lane], 16));
                Vector Offset;
                for (unsigned Axis = 0; Axis != 4; ++Axis)
                    Offset[Axis] = Parameter<float>(Block, FinalizeOffsets + Batch * 64 + Axis * 16 + Lane * 4);
                Results[Lane] = MultiplyQuaternion(Q, Offset);
            }
            for (unsigned Lane = 0; Lane != 4; ++Lane) Write(Destinations[Lane], Results[Lane], 16);
        }
        break;
    }
    case 51: // Aim constraint, u4 RVA 0x2055550 / 0x2050e40.
    {
        const auto Header = Param + Count * 4 + ((Count + 7) & ~7u) * 2;
        const auto Batches = Parameter<std::uint32_t>(Block, Header);
        auto UpMatrices = Header + Parameter<std::uint32_t>(Block, Header + 4);
        auto UpScalars = Header + Parameter<std::uint32_t>(Block, Header + 8);
        const auto FlagsTable = Header + Parameter<std::uint32_t>(Block, Header + 12);
        const auto Records = Header + 16;
        const auto Offsets = Records + Batches * 16;
        AccumulatePoint(Block, Count, Param, false);
        for (unsigned Batch = 0; Batch != Batches; ++Batch)
        {
            std::array<Vector, 4> Results;
            std::array<std::uint16_t, 4> Destinations;
            for (unsigned Lane = 0; Lane != 4; ++Lane)
            {
                const auto Record = Records + Batch * 16 + Lane * 4;
                Destinations[Lane] = Parameter<std::uint16_t>(Block, Record);
                const auto Parent = Read<Matrix>(Parameter<std::uint16_t>(Block, Record + 2), 16);
                Matrix Columns{};
                for (unsigned Column = 0; Column != 4; ++Column)
                for (unsigned Row = 0; Row != 3; ++Row)
                    Columns[Column][Row] = Parent[Row][Column] * (Column < 3 ? Parent[3][Column] : 1.0f);
                Columns[3][3] = 1;
                const auto Flags = Parameter<std::uint16_t>(Block, FlagsTable + Batch * 8 + Lane * 2) & 3u;
                Vector Up{0,0,1,0};
                if (Flags & 1)
                {
                    for (unsigned Axis = 0; Axis != 3; ++Axis)
                        Up[Axis] = Read<float>(Parameter<std::uint16_t>(Block, UpScalars + Axis * 2));
                    UpScalars += 6;
                }
                if (Flags & 2)
                {
                    const auto UpMatrix = Read<Matrix>(Parameter<std::uint16_t>(Block, UpMatrices), 16);
                    UpMatrices += 2;
                    if (Flags & 1)
                    {
                        const Vector Scaled{Up[0]*UpMatrix[3][0],Up[1]*UpMatrix[3][1],Up[2]*UpMatrix[3][2],0};
                        for (unsigned Row = 0; Row != 3; ++Row)
                            Up[Row] = (Scaled[0]*UpMatrix[Row][0]+Scaled[1]*UpMatrix[Row][1]) +
                                (Scaled[2]*UpMatrix[Row][2]+Scaled[3]*UpMatrix[Row][3]);
                    }
                    else
                        for (unsigned Row = 0; Row != 3; ++Row) Up[Row] = UpMatrix[Row][3] - Columns[3][Row];
                }
                std::array<Vector,4> Input{};
                Input[0] = Read<Vector>(Destinations[Lane],16);
                Input[1] = Read<Vector>((Destinations[Lane] & ~15u) + 16 + (Destinations[Lane] & 3));
                Input[2] = Up;
                std::memcpy(&Input[3], &Flags, 4);
                for (unsigned Axis = 0; Axis != 4; ++Axis)
                    Results[Lane][Axis] = Parameter<float>(Block,Offsets+Batch*64+Axis*16+Lane*4);
                SourceAim(std::as_writable_bytes(std::span(Results[Lane])),
                    std::as_writable_bytes(std::span(Input)),std::as_writable_bytes(std::span(Columns)));
            }
            for (unsigned Lane = 0; Lane != 4; ++Lane) Write(Destinations[Lane], Results[Lane], 16);
        }
        break;
    }
    case 52: // HierarchyCopyInConstants, u4 RVA 0x204a330 / wrapper 0x2055620.
    {
        const auto Words = Parameter<std::uint16_t>(Block, CommandOffset + 4);
        const auto Table = Parameter<std::uint16_t>(Block, CommandOffset + 8);
        const std::uint32_t Constants = Table + (Count + Words) * 16;
        for (std::uint32_t Batch = 0; Batch != Count; ++Batch)
        {
            std::array<Vector, 4> Values;
            std::array<std::uint16_t, 4> Destinations;
            for (std::uint32_t Lane = 0; Lane != 4; ++Lane)
            {
                const auto Pair = Table + Batch * 16 + Lane * 4;
                Destinations[Lane] = Parameter<std::uint16_t>(Block, Pair);
                Values[Lane] = Parameter<Vector>(Block, Constants + (Parameter<std::uint16_t>(Block, Pair + 2) & ~15u));
            }
            for (std::uint32_t Lane = 0; Lane != 4; ++Lane)
            {
                Write(Destinations[Lane], Values[Lane], 16);
            }
        }
        for (std::uint32_t Batch = 0; Batch != Words; ++Batch)
        {
            std::array<float, 4> Values;
            std::array<std::uint16_t, 4> Destinations;
            for (std::uint32_t Lane = 0; Lane != 4; ++Lane)
            {
                const auto Pair = Table + (Count + Batch) * 16 + Lane * 4;
                Destinations[Lane] = Parameter<std::uint16_t>(Block, Pair);
                Values[Lane] = Parameter<float>(Block, Constants + (Parameter<std::uint16_t>(Block, Pair + 2) & ~3u));
            }
            for (std::uint32_t Lane = 0; Lane != 4; ++Lane)
            {
                Write(Destinations[Lane], Values[Lane]);
            }
        }
        break;
    }
    case 53: // HierarchyCopyQuadwords: read a complete SIMD batch before writing it.
    {
        for (std::uint32_t Batch = 0; Batch < Count; Batch += 4)
        {
            std::array<Vector, 4> Values;
            std::array<std::uint16_t, 4> Destinations;
            for (std::uint32_t Lane = 0; Lane != 4; ++Lane)
            {
                const auto Pair = Param + (Batch + Lane) * 4;
                Destinations[Lane] = Parameter<std::uint16_t>(Block, Pair);
                Values[Lane] = Read<Vector>(Parameter<std::uint16_t>(Block, Pair + 2), 16);
            }
            for (std::uint32_t Lane = 0; Lane != 4; ++Lane)
            {
                Write(Destinations[Lane], Values[Lane], 16);
            }
        }
        break;
    }
    case 54: // HierarchyCopyWords, u4 RVA 0x2049ee0.
    {
        for (std::uint32_t Batch = 0; Batch < Count; Batch += 4)
        {
            std::array<float, 4> Values;
            std::array<std::uint16_t, 4> Destinations;
            for (std::uint32_t Lane = 0; Lane != 4; ++Lane)
            {
                const auto Pair = Param + (Batch + Lane) * 4;
                Destinations[Lane] = Parameter<std::uint16_t>(Block, Pair);
                Values[Lane] = Read<float>(Parameter<std::uint16_t>(Block, Pair + 2));
            }
            for (std::uint32_t Lane = 0; Lane != 4; ++Lane)
            {
                Write(Destinations[Lane], Values[Lane]);
            }
        }
        break;
    }
    case 128: // Cone measurement, u4 RVA 0x199a8f0.
    {
        for (unsigned I = 0; I != Count; ++I)
        {
            const auto RecordOffset = Param + I * 64;
            if (Parameter<std::uint32_t>(Block, RecordOffset + 32) == 0x10a581)
            {
                auto Record = Parameter<std::array<std::byte,64>>(Block, RecordOffset);
                std::array<Vector, 2> Input{};
                Input[0] = Read<Vector>(Parameter<std::uint32_t>(Block, RecordOffset + 36), 16);
                const auto AngleAddress = Parameter<std::uint32_t>(Block, RecordOffset + 48);
                if (AngleAddress != 0xffffffff) Input[1][0] = Read<float>(AngleAddress);
                Vector Result{};
                SourceCone(std::as_writable_bytes(std::span(Result)), std::as_writable_bytes(std::span(Input)), Record);
                Write(Parameter<std::uint32_t>(Block, RecordOffset + 60), Result[0]);
            }
        }
        break;
    }
    case 129: // Twist measurement, u4 RVA 0x1998ac0.
    {
        for (unsigned I = 0; I != Count; ++I)
        {
            const auto RecordOffset = Param + I * 64;
            if (Parameter<std::uint32_t>(Block, RecordOffset + 32) == 0x10a582)
            {
                auto Record = Parameter<std::array<std::byte,64>>(Block, RecordOffset);
                auto Quaternion = Read<Vector>(Parameter<std::uint32_t>(Block, RecordOffset + 36), 16);
                Vector Result{};
                SourceTwist(std::as_writable_bytes(std::span(Result)), std::as_writable_bytes(std::span(Quaternion)), Record);
                Write(Parameter<std::uint32_t>(Block, RecordOffset + 60), Result[0]);
            }
        }
        break;
    }
    case 131: // Scalar channels from SQT, u4 RVA 0x1998660.
    {
        const auto OutputCount = Parameter<std::uint32_t>(Block, Param + 4);
        const auto SourceOffset = Parameter<std::uint16_t>(Block, Param + 8);
        const auto Outputs = Param + Parameter<std::uint16_t>(Block, Param + 10);
        const auto Source = Parameter<std::uint32_t>(Block, Param + 12 + SourceOffset);
        const auto QuaternionAddress = (Source & ~15u) | (Source & 3);
        const auto Q = Read<Vector>(QuaternionAddress);
        const auto Translation = Read<Vector>(QuaternionAddress + 16);
        Vector Diagonal, Plus, Minus;
        for (std::uint32_t Axis = 0; Axis != 3; ++Axis)
        {
            const auto Next = (Axis + 1) % 3;
            const auto Last = (Axis + 2) % 3;
            Diagonal[Axis] = (1.0f - (Q[Next] + Q[Next]) * Q[Next]) - (Q[Last] + Q[Last]) * Q[Last];
            Plus[Axis] = (Q[3] + Q[3]) * Q[Next] + Q[Last] * (Q[Axis] + Q[Axis]);
            Minus[Axis] = Q[Next] * (Q[Axis] + Q[Axis]) - (Q[3] + Q[3]) * Q[Last];
        }
        const std::array<float, 9> Columns{Diagonal[0], Plus[1], Minus[2],
            Minus[0], Diagonal[1], Plus[2], Plus[0], Minus[1], Diagonal[2]};
        for (std::uint32_t I = 0; I != OutputCount; ++I)
        {
            const auto Channel = Parameter<std::uint32_t>(Block, Outputs + I * 8);
            float Value = 1.0f;
            if (Channel < 3) Value = Translation[Channel];
            else if (Channel < 7) Value = Read<float>(QuaternionAddress + (Channel - 3) * 4);
            else if (Channel < 16) Value = Columns[Channel - 7];
            Write(Parameter<std::uint32_t>(Block, Outputs + I * 8 + 4), Value);
        }
        break;
    }
    case 132: // Four-point surface constraint, u4 RVA 0x1999740.
    {
        const auto InputCount = Parameter<std::uint32_t>(Block, Param + 52);
        const auto OutputCount = Parameter<std::uint32_t>(Block, Param + 4);
        const auto DynamicPoints = Parameter<std::uint32_t>(Block, Param + 12) != 0;
        const auto Inputs = Param + Parameter<std::uint16_t>(Block, Param + 68);
        const auto Outputs = Param + Parameter<std::uint16_t>(Block, Param + 70);
        const auto Points = Param + Parameter<std::uint16_t>(Block, Param + 72);
        const auto Binds = Param + Parameter<std::uint16_t>(Block, Param + 74);
        const auto Weights = Param + Parameter<std::uint16_t>(Block, Param + 76);
        const auto PointInputs = Inputs + InputCount * 4;
        const auto Parent = Read<Matrix>(Parameter<std::uint32_t>(Block, PointInputs + (DynamicPoints ? 48 : 0)), 16);
        std::array<Vector,13> MathInput{};
        for (unsigned Column = 0; Column != 4; ++Column)
        for (unsigned Row = 0; Row != 3; ++Row)
            MathInput[9+Column][Row] = Parent[Row][Column] * (Column < 3 ? Parent[3][Column] : 1.0f);
        MathInput[12][3] = 1;
        const auto FirstDestination = Parameter<std::uint32_t>(Block, Outputs + 4);
        std::memcpy(&MathInput[8], &FirstDestination, 4);
        std::vector<Matrix> Matrices(InputCount), BindMatrices(InputCount);
        for (unsigned I = 0; I != InputCount; ++I)
        {
            const auto World = Read<Matrix>(Parameter<std::uint32_t>(Block, Inputs + I * 4), 16);
            for (unsigned Column = 0; Column != 4; ++Column)
            for (unsigned Row = 0; Row != 3; ++Row) Matrices[I][Column][Row] = World[Row][Column];
            Matrices[I][3][3] = 1;
            BindMatrices[I] = Parameter<Matrix>(Block, Binds + I * 64);
        }
        for (unsigned Point = 0; Point != 4; ++Point)
        {
            if (DynamicPoints)
            {
                for (unsigned Axis = 0; Axis != 3; ++Axis)
                    MathInput[Point][Axis] = Read<float>(Parameter<std::uint32_t>(Block, PointInputs + Point * 12 + Axis * 4));
                MathInput[Point][3] = MathInput[Point][1];
            }
            else MathInput[Point] = Parameter<Vector>(Block, Points + Point * 16);
        }
        for (unsigned Point = 0; Point != 4; ++Point)
        for (unsigned I = 0; I != InputCount; ++I)
        {
            const auto& P = MathInput[Point];
            const auto& Bind = BindMatrices[I];
            const auto& World = Matrices[I];
            Vector Local, Position;
            for (unsigned Axis = 0; Axis != 4; ++Axis)
                Local[Axis] = ((P[1]*Bind[1][Axis]+P[0]*Bind[0][Axis])+P[2]*Bind[2][Axis])+Bind[3][Axis];
            for (unsigned Axis = 0; Axis != 4; ++Axis)
                Position[Axis] = ((Local[1]*World[1][Axis]+Local[0]*World[0][Axis])+Local[2]*World[2][Axis])+World[3][Axis];
            const auto Weight = Parameter<float>(Block, Weights + (Point * InputCount + I) * 4);
            for (unsigned Axis = 0; Axis != 4; ++Axis)
                MathInput[4+Point][Axis] = Position[Axis] * Weight + MathInput[4+Point][Axis];
        }
        std::array<float,8> Results{};
        auto Parameters = Parameter<std::array<std::byte,80>>(Block, Param);
        SourceSurface(std::as_writable_bytes(std::span(Results)),std::as_writable_bytes(std::span(MathInput)),Parameters);
        for (unsigned I = 0; I != OutputCount; ++I)
        {
            const auto Channel = Parameter<std::uint32_t>(Block, Outputs + I * 8);
            if (Channel <= 7) Write(Parameter<std::uint32_t>(Block, Outputs + I * 8 + 4), Results[Channel]);
        }
        break;
    }
    case 133: // Weighted vectors, u4 RVA 0x199b1f0; inputs clamped to [-1,1].
    {
        const auto InputCount = Parameter<std::uint32_t>(Block, Param + 4);
        const auto OutputCount = Parameter<std::uint32_t>(Block, Param + 8);
        const auto Inputs = Param + Parameter<std::uint16_t>(Block, Param + 12);
        const auto Outputs = Param + Parameter<std::uint16_t>(Block, Param + 14);
        const auto Coefficients = Param + Parameter<std::uint16_t>(Block, Param + 16);
        std::vector<float> InputValues(InputCount);
        std::vector<Vector> OutputValues(OutputCount);
        for (std::uint32_t I = 0; I != InputCount; ++I)
        {
            const float Value = Read<float>(Parameter<std::uint32_t>(Block, Inputs + I * 4));
            InputValues[I] = !(1.0f >= Value) ? 1.0f : Value > -1.0f ? Value : -1.0f;
        }
        for (std::uint32_t Output = 0; Output != OutputCount; ++Output)
        {
            Vector Sum{};
            for (std::uint32_t I = 0; I != InputCount; ++I)
            {
                const auto Coefficient = Parameter<Vector>(Block, Coefficients + (I * OutputCount + Output) * 16);
                for (std::uint32_t Lane = 0; Lane != 4; ++Lane)
                    Sum[Lane] = InputValues[I] * Coefficient[Lane] + Sum[Lane];
            }
            OutputValues[Output] = Sum;
        }
        for (std::uint32_t Output = 0; Output != OutputCount; ++Output)
        for (std::uint32_t Lane = 0; Lane != 3; ++Lane)
            Write(Parameter<std::uint32_t>(Block, Outputs + Output * 12 + Lane * 4), OutputValues[Output][Lane]);
        break;
    }
    case 130: // Original RBF: distance kernel 0x1997ed0 returns its input unchanged.
    {
        const auto InputCount = Parameter<std::uint32_t>(Block, Param + 4);
        const auto OutputCount = Parameter<std::uint32_t>(Block, Param + 8);
        const auto CenterCount = Parameter<std::uint32_t>(Block, Param + 12);
        const auto Inputs = Param + Parameter<std::uint16_t>(Block, Param + 20);
        const auto Outputs = Param + Parameter<std::uint16_t>(Block, Param + 22);
        const auto Centers = Param + Parameter<std::uint16_t>(Block, Param + 24);
        const auto Coefficients = Param + Parameter<std::uint16_t>(Block, Param + 26);
        std::vector<float> InputValues(InputCount);
        std::vector<float> Distances(CenterCount);
        std::vector<float> OutputValues(OutputCount);
        for (std::uint32_t I = 0; I != InputCount; ++I)
        {
            InputValues[I] = Read<float>(Parameter<std::uint32_t>(Block, Inputs + I * 4));
        }
        for (std::uint32_t Center = 0; Center != CenterCount; ++Center)
        {
            float Sum = 0;
            for (std::uint32_t I = 0; I < InputCount; I += 4)
            {
                Vector Squared{};
                for (std::uint32_t Lane = 0; Lane != 4 && I + Lane < InputCount; ++Lane)
                {
                    const float Delta = InputValues[I + Lane] - Parameter<float>(Block, Centers + (Center * InputCount + I + Lane) * 4);
                    Squared[Lane] = Delta * Delta;
                }
                Sum += I + 4 <= InputCount ? (Squared[0] + Squared[1]) + (Squared[2] + Squared[3])
                    : Squared[2] + (Squared[0] + Squared[1]);
            }
            Distances[Center] = Sum <= 0 ? 0 : std::sqrt(Sum);
        }
        for (std::uint32_t Output = 0; Output != OutputCount; ++Output)
        {
            float Sum = 0;
            for (std::uint32_t Center = 0; Center < CenterCount; Center += 4)
            {
                Vector Products{};
                for (std::uint32_t Lane = 0; Lane != 4 && Center + Lane < CenterCount; ++Lane)
                {
                    Products[Lane] = Distances[Center + Lane] * Parameter<float>(Block, Coefficients + ((Center + Lane) * OutputCount + Output) * 4);
                }
                Sum += Center + 4 <= CenterCount ? (Products[0] + Products[1]) + (Products[2] + Products[3])
                    : Products[2] + (Products[0] + Products[1]);
            }
            OutputValues[Output] = Sum;
        }
        for (std::uint32_t Output = 0; Output != OutputCount; ++Output)
        {
            Write(Parameter<std::uint32_t>(Block, Outputs + Output * 4), OutputValues[Output]);
        }
        break;
    }
    }
}
}
