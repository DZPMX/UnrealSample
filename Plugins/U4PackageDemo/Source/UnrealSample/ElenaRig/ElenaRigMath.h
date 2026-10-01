#pragma once
#include <span>
#include <cstddef>
#include <cstdint>
namespace ElenaRig
{
void SourceAsin(std::span<std::byte> A, std::span<std::byte> B, std::span<std::byte> C);
void SourceAtan(std::span<std::byte> A, std::span<std::byte> B, std::span<std::byte> C);
void SourceAtan2(std::span<std::byte> A, std::span<std::byte> B, std::span<std::byte> C);
void SourceSinCos(std::span<std::byte> A, std::span<std::byte> B, std::span<std::byte> C);
void SourceQuaternionMultiply(std::span<std::byte> A, std::span<std::byte> B, std::span<std::byte> C);
void SourceQuaternionToEuler(std::span<std::byte> A, std::span<std::byte> B, std::span<std::byte> C);
void SourceEulerToQuaternion(std::span<std::byte> A, std::span<std::byte> B, std::span<std::byte> C);
void SourceTwist(std::span<std::byte> A, std::span<std::byte> B, std::span<std::byte> C);
void SourceCone(std::span<std::byte> A, std::span<std::byte> B, std::span<std::byte> C);
void SourceMatrixToQuaternion(std::span<std::byte> A, std::span<std::byte> B, std::span<std::byte> C);
void SourceMatrixMultiply(std::span<std::byte> A, std::span<std::byte> B, std::span<std::byte> C);
void SourceMatrixInverse(std::span<std::byte> A, std::span<std::byte> B, std::span<std::byte> C);
void SourceOrthonormalize(std::span<std::byte> A, std::span<std::byte> B, std::span<std::byte> C);
void SourceColumnInverse(std::span<std::byte> A, std::span<std::byte> B, std::span<std::byte> C);
void SourceColumnQuaternion(std::span<std::byte> A, std::span<std::byte> B, std::span<std::byte> C);
void SourceAim(std::span<std::byte> A, std::span<std::byte> B, std::span<std::byte> C);
void SourceRotationX(std::span<std::byte> A, std::span<std::byte> B, std::span<std::byte> C);
void SourceRotationY(std::span<std::byte> A, std::span<std::byte> B, std::span<std::byte> C);
void SourceRotationZ(std::span<std::byte> A, std::span<std::byte> B, std::span<std::byte> C);
void SourceColumnMultiply(std::span<std::byte> A, std::span<std::byte> B, std::span<std::byte> C);
void SourceColumnEuler(std::span<std::byte> A, std::span<std::byte> B, std::span<std::byte> C);
void SourceSurface(std::span<std::byte> A, std::span<std::byte> B, std::span<std::byte> C);
}
