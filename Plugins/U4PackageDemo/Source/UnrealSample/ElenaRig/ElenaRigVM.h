#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace ElenaRig
{
using Vector = std::array<float, 4>;
using Matrix = std::array<Vector, 4>;
using SQT = std::array<Vector, 3>;

struct Group
{
    std::uint32_t FirstBone = 0;
    std::uint32_t BoneCount = 0;
    std::vector<std::uint32_t> Blocks;
};

struct Program
{
    std::vector<Group> Groups;
    std::vector<std::vector<std::byte>> Blocks;
    std::vector<SQT> ReferencePose;
};

Program ReadProgram(std::span<const std::byte> Data);

class VM
{
public:
    VM();
    void Evaluate(const Program& ProgramData, std::span<const SQT> Input);
    void Execute(std::span<const std::byte> Block, std::uint32_t CommandOffset);

    // Kept per animation instance. Bank 3 carries dependencies between groups.
    std::array<std::vector<std::byte>, 4> Banks;
    std::vector<Matrix> Worlds;
    std::vector<SQT> Locals;

private:
    void AccumulatePoint(std::span<const std::byte> Block, std::uint32_t Count, std::uint32_t Table, bool ParentPosition);
    void NormalizePoint(std::span<const std::byte> Block, std::uint32_t Count, std::uint32_t Table, bool AddOffset);
    void LocalizePoint(std::span<const std::byte> Block, std::uint32_t Count, std::uint32_t Table, bool AddOffset);

    template<class T> T Read(std::uint32_t Address, std::uint32_t Alignment = 4) const
    {
        T Value;
        std::memcpy(&Value, Banks[Address & 3].data() + (Address & ~(Alignment - 1)), sizeof(T));
        return Value;
    }

    template<class T> void Write(std::uint32_t Address, const T& Value, std::uint32_t Alignment = 4)
    {
        std::memcpy(Banks[Address & 3].data() + (Address & ~(Alignment - 1)), &Value, sizeof(T));
    }
};

template<class T> T Parameter(std::span<const std::byte> Block, std::uint32_t Offset)
{
    T Value;
    std::memcpy(&Value, Block.data() + Offset, sizeof(T));
    return Value;
}
}
