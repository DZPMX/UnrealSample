#include "GraceWrinkleMotion.h"

namespace
{
    constexpr float C16 = 1.5259021893143654e-05f;
    constexpr float C24 = 5.960465188081798e-08f;
    constexpr float C5 = 0.032258063554763794f;

    float ReadFloat(const TArray<uint8>& Bytes, int32 Offset)
    {
        float Value;
        FMemory::Memcpy(&Value, &Bytes[Offset], sizeof(float));
        return Value;
    }

    uint32 ReadInteger(const FGraceWrinkleTrack& Track, int32 Index)
    {
        if (Track.Compression == 0x31000)
        {
            const int32 At = Index * 3;
            return (uint32(Track.Packed[At]) << 16) | (uint32(Track.Packed[At + 1]) << 8) | Track.Packed[At + 2];
        }
        const int32 At = Index * 2;
        const uint32 Word = uint32(Track.Packed[At]) | (uint32(Track.Packed[At + 1]) << 8);
        return Track.Compression == 0x20000 ? Word & 31u : Word;
    }
}

float FGraceWrinkleTrack::Single(int32 Index) const
{
    if (Compression == 0)
    {
        return ReadFloat(Packed, Index * 12);
    }
    if (Compression == 0x41000)
    {
        return ReadFloat(Packed, Index * 4);
    }
    const float Integer = float(ReadInteger(*this, Index));
    const float Scaled = Integer * Unpack[0];
    const float Constant = Compression == 0x21000 ? C16 : (Compression == 0x31000 ? C24 : C5);
    const float Normalized = Scaled * Constant;
    return Compression == 0x20000 ? Unpack[3] + Normalized : Normalized + Unpack[1];
}

float FGraceWrinkleTrack::Pair(int32 Index, float Fraction) const
{
    // Original JBE comparisons select the first key for unordered fractions.
    if (FMath::IsNaN(Fraction) || Fraction <= 0.0f)
    {
        return Single(Index);
    }
    if (1.0f <= Fraction)
    {
        return Single(Index + 1);
    }
    if (Compression == 0 || Compression == 0x41000)
    {
        const float Previous = Single(Index);
        const float Difference = Single(Index + 1) - Previous;
        const float Product = Difference * Fraction;
        return Previous + Product;
    }
    const float Previous = float(ReadInteger(*this, Index));
    const float Following = float(ReadInteger(*this, Index + 1));
    const float Difference = Following - Previous;
    const float Product = Difference * Fraction;
    const float Interpolated = Previous + Product;
    if (Compression == 0x20000)
    {
        // 14590d740: interpolate packed components, then scale, then normalize.
        const float Scaled = Interpolated * Unpack[0];
        const float Normalized = Scaled * C5;
        return Unpack[3] + Normalized;
    }
    const float Scale = Unpack[0] * (Compression == 0x21000 ? C16 : C24);
    const float Scaled = Scale * Interpolated;
    return Scaled + Unpack[1];
}

float FGraceWrinkleTrack::Sample(float Frame, float ResourceLastFrame, int32& CachedIndex) const
{
    // 14556bbd0/14590d090, retaining cached forward/backward search boundaries.
    const int32 Count = Frames.Num();
    if (Count == 1)
    {
        return Single(0);
    }
    int32 Index = CachedIndex;
    float Point = float(Frames[Index]);
    if (Point > Frame)
    {
        const float Distance = Point - Frame;
        if (Distance > Frame)
        {
            Index = 0;
            Point = 0.0f;
        }
    }
    else
    {
        const float Remaining = ResourceLastFrame - Point;
        const float Distance = Frame - Point;
        if (Distance > Remaining)
        {
            Index = Count - 1;
            Point = ResourceLastFrame;
        }
    }
    if (!FMath::IsNaN(Frame) && !FMath::IsNaN(Point) && Frame >= Point)
    {
        ++Index;
        for (;;)
        {
            const float Previous = Point;
            if (Index == Count)
            {
                CachedIndex = Count - 1;
                return Single(Count - 1);
            }
            Point = float(Frames[Index]);
            ++Index;
            if (!FMath::IsNaN(Frame) && Point > Frame)
            {
                Index -= 2;
                CachedIndex = Index;
                const float Numerator = Frame - Previous;
                const float Denominator = Point - Previous;
                return Pair(Index, Numerator / Denominator);
            }
        }
    }
    const int32 OldIndex = Index;
    int32 Traversed = 0;
    for (;;)
    {
        const float Following = Point;
        if (OldIndex == Traversed)
        {
            CachedIndex = 0;
            const float Numerator = Frame - 0.0f;
            const float Denominator = float(Frames[1]) - 0.0f;
            return Pair(0, Numerator / Denominator);
        }
        Point = float(Frames[OldIndex - Traversed - 1]);
        ++Traversed;
        if (!FMath::IsNaN(Frame) && Frame >= Point)
        {
            CachedIndex = OldIndex - Traversed + 1;
            const float Numerator = Frame - Point;
            const float Denominator = Following - Point;
            return Pair(OldIndex - Traversed, Numerator / Denominator);
        }
    }
}

TArray<float> UGraceWrinkleMotion::SampleAtSeconds(float Seconds) const
{
    TArray<float> Values;
    Values.SetNumZeroed(32);
    const float Frame = Seconds * SourceFrameRate;
    for (const FGraceWrinkleTrack& Track : Tracks)
    {
        int32 CachedIndex = 0;
        Values[Track.MaskIndex] = Track.Sample(Frame, SourceLastFrame, CachedIndex);
    }
    return Values;
}
