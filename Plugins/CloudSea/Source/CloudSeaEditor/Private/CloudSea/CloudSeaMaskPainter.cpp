// Copyright KuaiShou Games, Inc. All Rights Reserved.
#include "CloudSeaMaskPainter.h"
#include "CloudSeaMaskPaintData.h"

#include "CloudSeaComponent.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "CanvasItem.h"
#include "ComponentVisualizer.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "EditorModeManager.h"
#include "EditorModeRegistry.h"
#include "EditorModes.h"
#include "EditorUndoClient.h"
#include "EditorViewportClient.h"
#include "EdMode.h"
#include "Engine/Texture2D.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "IDetailCustomization.h"
#include "Misc/CoreDelegates.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "PrimitiveDrawingUtils.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "SceneView.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "TextureCompiler.h"
#include "TextureResource.h"
#include "UnrealEdGlobals.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CloudSeaMaskPainter"

namespace
{
const FEditorModeID MaskModeId(TEXT("EM_CloudSeaMaskPaint"));
float BrushRadiusMeters = 50.0f;
float BrushStrength = 1.0f;
float BrushHardness = 0.5f;
int32 NewMaskResolution = 1024;
bool bPaintWhite = false;
TWeakObjectPtr<AActor> MaskDebugOwner;
FName MaskDebugComponentName;
uint64 MaskDebugRevision = 0;
FDelegateHandle MaskDebugRegisterHandle;
bool bMaskDebugRegistered = false;

bool IsMaskDebugEnabled(const UCloudSeaComponent* Target)
{
	return IsValid(Target) && Target->GetOwner() == MaskDebugOwner.Get() && Target->GetFName() == MaskDebugComponentName;
}

UCloudSeaComponent* FindMaskComponent(AActor* CloudActor, FName ComponentName)
{
	if (IsValid(CloudActor))
	{
		TInlineComponentArray<UCloudSeaComponent*> Components(CloudActor);
		for (UCloudSeaComponent* Component : Components)
			if (Component->GetFName() == ComponentName) return Component;
	}
	return nullptr;
}

struct FCloudSeaMaskDraft
{
	TStrongObjectPtr<UCloudSeaMaskPaintData> Data;
	TStrongObjectPtr<UTexture2D> Texture;
	TWeakObjectPtr<UTexture2D> OriginalAssignedTexture;
	TWeakObjectPtr<AActor> Actor;
	FName ComponentName;
	TArray<uint8> OriginalPixels;
	FVector2D OriginalCenter = FVector2D::ZeroVector;
	FVector2D OriginalWorldSize = FVector2D::ZeroVector;
	bool bNewTexture = false;

	UCloudSeaComponent* GetTarget() const
	{
		return FindMaskComponent(Actor.Get(), ComponentName);
	}
	bool Matches(const UCloudSeaComponent* Target) const { return Data.Get() && Target && GetTarget() == Target; }
	bool HasOriginalMapping(const UCloudSeaComponent* Target) const
	{
		return Target && Target->CloudMaskCenterWorldXY == OriginalCenter && Target->CloudMaskWorldSize == OriginalWorldSize;
	}
	bool IsModified() const
	{
		return Data.Get() && (bNewTexture || Data->Pixels != OriginalPixels
			|| Data->CenterWorldXY != OriginalCenter || Data->WorldSize != OriginalWorldSize);
	}
	void Reset()
	{
		Data.Reset();
		Texture.Reset();
		OriginalAssignedTexture.Reset();
		Actor.Reset();
		ComponentName = NAME_None;
		OriginalPixels.Empty();
		OriginalCenter = FVector2D::ZeroVector;
		OriginalWorldSize = FVector2D::ZeroVector;
		bNewTexture = false;
	}
};

// Keep a suspended draft in memory if changing editor mode or reconstructing its actor.
// Neither this canvas nor the component's transient preview can be saved by Save All.
FCloudSeaMaskDraft MaskDraft;
bool CommitMaskDraft();
bool ConfirmFinishMaskDraft();
void DiscardMaskDraft();
bool BeginMaskDraft(UCloudSeaComponent* Target, bool bNewTexture);
void DrawActiveMaskDebug(const UCloudSeaComponent* Target, const FSceneView* View, FPrimitiveDrawInterface* PDI);
void DrawActiveMaskDebugHUD(const UCloudSeaComponent* Target, const FViewport* Viewport, const FSceneView* View, FCanvas* Canvas);

bool CanPaintComponent(const UCloudSeaComponent* Component)
{
	return IsValid(Component) && Component->GetWorld()
		&& Component->GetWorld()->WorldType == EWorldType::Editor
		&& !Component->IsTemplate() && GEditor && !GEditor->PlayWorld
		&& Component->bEnableCloudMask
		&& Component->VerticalRenderingMode == ECloudSeaVerticalRenderingMode::SingleLayerWithoutPrefill;
}

// Keep the editable asset single-channel and linear. Do not silently convert an
// arbitrary imported texture or change its compression settings while painting.
bool ReadMask(UTexture2D* Texture, TArray64<uint8>& Pixels)
{
	if (!IsValid(Texture) || !Texture->IsAsset() || Texture->SRGB || Texture->VirtualTextureStreaming)
	{
		return false;
	}
	UTexture* Textures[] = { Texture };
	FTextureCompilingManager::Get().FinishCompilation(Textures);
	return Texture->Source.IsValid() && Texture->Source.GetFormat() == TSF_G8
		&& Texture->Source.GetNumBlocks() == 1 && Texture->Source.GetNumLayers() == 1
		&& Texture->Source.GetSizeX() >= 1 && Texture->Source.GetSizeX() <= 2048
		&& Texture->Source.GetSizeY() >= 1 && Texture->Source.GetSizeY() <= 2048
		&& Texture->Source.GetMipData(Pixels, 0)
		&& Pixels.Num() == int64(Texture->Source.GetSizeX()) * Texture->Source.GetSizeY();
}

FBox2D StrokeWorldBounds(const FCloudSeaMaskPaintStroke& Stroke)
{
	FBox2D Bounds(ForceInit);
	const FVector2D Radius = FVector2D(FMath::Max(double(Stroke.RadiusMeters), 0.01) * 100.0);
	for (const FVector2D& Point : Stroke.Points)
	{
		Bounds += Point - Radius;
		Bounds += Point + Radius;
	}
	return Bounds;
}

FIntRect MaskPixelRect(const FBox2D& Bounds, const FVector2D& Center, const FVector2D& Size, int32 Width, int32 Height)
{
	const FVector2D Corner = Center - Size * 50.0;
	const FVector2D Texel(Size.X * 100.0 / Width, Size.Y * 100.0 / Height);
	return FIntRect(
		FMath::FloorToInt(FMath::Clamp((Bounds.Min.X - Corner.X) / Texel.X, 0.0, double(Width))),
		FMath::FloorToInt(FMath::Clamp((Bounds.Min.Y - Corner.Y) / Texel.Y, 0.0, double(Height))),
		FMath::CeilToInt(FMath::Clamp((Bounds.Max.X - Corner.X) / Texel.X, 0.0, double(Width))),
		FMath::CeilToInt(FMath::Clamp((Bounds.Max.Y - Corner.Y) / Texel.Y, 0.0, double(Height))));
}

// Source pixels cover BaseRect; a complete stroke applies strength once per pixel.
// A small saved rectangle is sufficient when replaying a finished stroke.
FIntRect RasterizeMaskSegment(TArray64<uint8>& Pixels, const TArray64<uint8>& Before, const FIntRect& BaseRect,
	const UCloudSeaMaskPaintData& Data, const FCloudSeaMaskPaintStroke& Stroke, const FVector2D& A, const FVector2D& B)
{
	const double Radius = FMath::Max(double(Stroke.RadiusMeters), 0.01) * 100.0;
	FBox2D Bounds(ForceInit);
	Bounds += A - FVector2D(Radius);
	Bounds += A + FVector2D(Radius);
	Bounds += B - FVector2D(Radius);
	Bounds += B + FVector2D(Radius);
	const FIntRect Rect = MaskPixelRect(Bounds, Data.CenterWorldXY, Data.WorldSize, Data.Width, Data.Height);
	const FVector2D Corner = Data.CenterWorldXY - Data.WorldSize * 50.0;
	const FVector2D Texel(Data.WorldSize.X * 100.0 / Data.Width, Data.WorldSize.Y * 100.0 / Data.Height);
	const FVector2D AB = B - A;
	const double LengthSquared = AB.SizeSquared();
	for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
	{
		for (int32 X = Rect.Min.X; X < Rect.Max.X; ++X)
		{
			const FVector2D P = Corner + FVector2D((X + 0.5) * Texel.X, (Y + 0.5) * Texel.Y);
			const double T = LengthSquared > 0.0 ? FMath::Clamp(FVector2D::DotProduct(P - A, AB) / LengthSquared, 0.0, 1.0) : 0.0;
			const double Distance = (P - (A + AB * T)).Size() / Radius;
			if (Distance >= 1.0) continue;
			const double Edge = FMath::Clamp((1.0 - Distance) / FMath::Max(1.0 - Stroke.Hardness, 0.001), 0.0, 1.0);
			const double Weight = Edge * Edge * (3.0 - 2.0 * Edge) * Stroke.Strength;
			const int64 Index = int64(Y) * Data.Width + X;
			const int64 BaseIndex = int64(Y - BaseRect.Min.Y) * BaseRect.Width() + X - BaseRect.Min.X;
			const uint8 Value = uint8(FMath::Clamp(FMath::RoundToInt(FMath::Lerp(double(Before[BaseIndex]), Stroke.bPaintWhite ? 255.0 : 0.0, Weight)), 0, 255));
			Pixels[Index] = Stroke.bPaintWhite ? FMath::Max(Pixels[Index], Value) : FMath::Min(Pixels[Index], Value);
		}
	}
	return Rect;
}

FBox2D OriginalMaskBounds(const UCloudSeaMaskPaintData& Data)
{
	int32 MinX = Data.Width, MinY = Data.Height, MaxX = -1, MaxY = -1;
	for (int32 Y = 0; Y < Data.Height; ++Y)
		for (int32 X = 0; X < Data.Width; ++X)
			if (Data.Pixels[Y * Data.Width + X] < 255)
			{
				MinX = FMath::Min(MinX, X); MinY = FMath::Min(MinY, Y);
				MaxX = FMath::Max(MaxX, X); MaxY = FMath::Max(MaxY, Y);
			}
	if (MaxX < 0) return FBox2D(ForceInit);
	// Include the bilinear filter footprint, clipped to the original authored rectangle.
	const FVector2D Texel(Data.WorldSize.X * 100.0 / Data.Width, Data.WorldSize.Y * 100.0 / Data.Height);
	const FVector2D Corner = Data.CenterWorldXY - Data.WorldSize * 50.0;
	return FBox2D(Corner + FVector2D(FMath::Max(MinX - 1, 0) * Texel.X, FMath::Max(MinY - 1, 0) * Texel.Y),
		Corner + FVector2D(FMath::Min(MaxX + 2, Data.Width) * Texel.X, FMath::Min(MaxY + 2, Data.Height) * Texel.Y));
}

uint8 SampleOriginalMask(const FVector2D& WorldPoint, int32 Width, int32 Height)
{
	const FVector2D UV((WorldPoint.X - MaskDraft.OriginalCenter.X) / (FMath::Max(MaskDraft.OriginalWorldSize.X, 1.0) * 100.0) + 0.5,
		(WorldPoint.Y - MaskDraft.OriginalCenter.Y) / (FMath::Max(MaskDraft.OriginalWorldSize.Y, 1.0) * 100.0) + 0.5);
	// Match the material: white outside the rectangle, clamped bilinear sampling inside.
	if (UV.X < 0.0 || UV.X > 1.0 || UV.Y < 0.0 || UV.Y > 1.0) return 255;
	const double X = UV.X * Width - 0.5, Y = UV.Y * Height - 0.5;
	const int32 FloorX = FMath::FloorToInt(X), FloorY = FMath::FloorToInt(Y);
	const int32 X0 = FMath::Clamp(FloorX, 0, Width - 1), X1 = FMath::Clamp(FloorX + 1, 0, Width - 1);
	const int32 Y0 = FMath::Clamp(FloorY, 0, Height - 1), Y1 = FMath::Clamp(FloorY + 1, 0, Height - 1);
	const double Top = FMath::Lerp(double(MaskDraft.OriginalPixels[Y0 * Width + X0]), double(MaskDraft.OriginalPixels[Y0 * Width + X1]), X - FloorX);
	const double Bottom = FMath::Lerp(double(MaskDraft.OriginalPixels[Y1 * Width + X0]), double(MaskDraft.OriginalPixels[Y1 * Width + X1]), X - FloorX);
	return uint8(FMath::Clamp(FMath::RoundToInt(FMath::Lerp(Top, Bottom, Y - FloorY)), 0, 255));
}

class FCloudSeaMaskEdMode : public FEdMode, public FEditorUndoClient
{
public:
	TWeakObjectPtr<AActor> TargetOwner;
	FName ComponentName;
	TObjectPtr<UTexture2D> Texture = nullptr;
	TObjectPtr<UTexture2D> Preview = nullptr;
	TObjectPtr<UCloudSeaMaskPaintData> PaintData = nullptr;
	TArray64<uint8> Pixels;
	TArray64<uint8> StrokeBefore;
	// Accumulate all edits until the shared debug cache consumes them, including edits while Debug is off.
	FIntRect MaskDebugDirtyRect = FIntRect(0, 0, 0, 0);
	bool bMaskDebugFullRebuild = true;
	TUniquePtr<FScopedTransaction> StrokeTransaction;
	FVector Hover = FVector::ZeroVector;
	FVector2D LastStrokePoint;
	FEditorViewportClient* HoverViewport = nullptr;
	FEditorViewportClient* StrokeViewport = nullptr;
	bool bHasHover = false;
	bool bInStroke = false;
	bool bStrokeChanged = false;
	bool bEraseStroke = false;
	int32 Width = 0;
	int32 Height = 0;

	UCloudSeaComponent* GetTarget() const
	{
		return FindMaskComponent(TargetOwner.Get(), ComponentName);
	}

	virtual bool UsesToolkits() const override { return false; }
	virtual bool UsesTransformWidget() const override { return false; }
	virtual bool DisallowMouseDeltaTracking() const override { return bInStroke; }
	virtual bool GetCursor(EMouseCursor::Type& OutCursor) const override
	{
		OutCursor = bHasHover ? EMouseCursor::Crosshairs : EMouseCursor::SlashedCircle;
		return true;
	}
	virtual bool IsCompatibleWith(FEditorModeID OtherMode) const override
	{
		return OtherMode == FBuiltinEditorModes::EM_Default;
	}
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		FEdMode::AddReferencedObjects(Collector);
		Collector.AddReferencedObject(Texture);
		Collector.AddReferencedObject(Preview);
		Collector.AddReferencedObject(PaintData);
	}
	virtual void Enter() override
	{
		FEdMode::Enter();
		GEditor->RegisterForUndo(this);
	}
	virtual void Exit() override
	{
		EndStroke();
		if (UCloudSeaComponent* Target = GetTarget()) Target->SetCloudMaskPaintPreview(nullptr);
		if (GEditor) GEditor->UnregisterForUndo(this);
		TargetOwner.Reset();
		Texture = nullptr;
		Preview = nullptr;
		PaintData = nullptr;
		Pixels.Empty();
		StrokeBefore.Empty();
		bHasHover = false;
		HoverViewport = nullptr;
		StrokeViewport = nullptr;
		FEdMode::Exit();
	}
	bool Start(UCloudSeaComponent* Target)
	{
		if (!CanPaintComponent(Target) || !MaskDraft.Matches(Target)) return false;
		TargetOwner = Target->GetOwner();
		ComponentName = Target->GetFName();
		PaintData = MaskDraft.Data.Get();
		Texture = Target->CloudMaskTexture;
		Width = PaintData->Width;
		Height = PaintData->Height;
		Pixels.Empty();
		Pixels.Append(PaintData->Pixels.GetData(), PaintData->Pixels.Num());
		Preview = UTexture2D::CreateTransient(Width, Height, PF_G8, NAME_None, Pixels);
		if (!Preview) return false;
		Preview->SRGB = false;
		Preview->NeverStream = true;
		Preview->Filter = TF_Bilinear;
		Preview->AddressX = TA_Clamp;
		Preview->AddressY = TA_Clamp;
		Preview->UpdateResource();
		bMaskDebugFullRebuild = true;
		MaskDebugDirtyRect = FIntRect(0, 0, 0, 0);
		++MaskDebugRevision;
		Target->SetCloudMaskPaintPreview(Preview, PaintData->CenterWorldXY, PaintData->WorldSize);
		return true;
	}

	bool FitCanvasToPaintedBounds()
	{
		if (!PaintData->PaintedWorldBounds.bIsValid) return false;
		const FVector2D NewCenter = PaintData->PaintedWorldBounds.GetCenter();
		const FVector2D SpanMeters = PaintData->PaintedWorldBounds.GetSize() / 100.0;
		// Leave one texel of white padding and grow with headroom, keeping the texture resolution fixed.
		const FVector2D RequiredSize(SpanMeters.X * Width / FMath::Max(Width - 2, 1),
			SpanMeters.Y * Height / FMath::Max(Height - 2, 1));
		FVector2D NewSize = PaintData->WorldSize;
		if (RequiredSize.X > NewSize.X) NewSize.X = RequiredSize.X * 1.25;
		if (RequiredSize.Y > NewSize.Y) NewSize.Y = RequiredSize.Y * 1.25;
		if (NewCenter == PaintData->CenterWorldXY && NewSize == PaintData->WorldSize) return false;
		PaintData->CenterWorldXY = NewCenter;
		PaintData->WorldSize = NewSize;
		return true;
	}

	void RebuildMappedPixels()
	{
		bMaskDebugFullRebuild = true;
		MaskDebugDirtyRect = FIntRect(0, 0, 0, 0);
		// Always reproject the pristine source and world-space paths. Never resample
		// the last preview, which would progressively blur it after repeated recentering.
		if (!PaintData->bUseOriginalPixels)
		{
			Pixels.Init(255, int64(Width) * Height);
		}
		else if (PaintData->CenterWorldXY == MaskDraft.OriginalCenter && PaintData->WorldSize == MaskDraft.OriginalWorldSize)
		{
			Pixels.Empty();
			Pixels.Append(MaskDraft.OriginalPixels.GetData(), MaskDraft.OriginalPixels.Num());
		}
		else
		{
			Pixels.SetNumUninitialized(int64(Width) * Height);
			for (int32 Y = 0; Y < Height; ++Y)
				for (int32 X = 0; X < Width; ++X)
				{
					const FVector2D WorldPoint = PaintData->CenterWorldXY + FVector2D(
						((X + 0.5) / Width - 0.5) * PaintData->WorldSize.X * 100.0,
						((Y + 0.5) / Height - 0.5) * PaintData->WorldSize.Y * 100.0);
					Pixels[int64(Y) * Width + X] = SampleOriginalMask(WorldPoint, Width, Height);
				}
		}
		for (int32 StrokeIndex = 0; StrokeIndex < PaintData->Strokes.Num(); ++StrokeIndex)
		{
			const FCloudSeaMaskPaintStroke& Stroke = PaintData->Strokes[StrokeIndex];
			if (Stroke.Points.IsEmpty()) continue;
			const bool bCurrentStroke = bInStroke && StrokeIndex == PaintData->Strokes.Num() - 1;
			FIntRect BaseRect(0, 0, Width, Height);
			TArray64<uint8> LocalBefore;
			if (bCurrentStroke)
			{
				StrokeBefore = Pixels;
			}
			else
			{
				BaseRect = MaskPixelRect(StrokeWorldBounds(Stroke), PaintData->CenterWorldXY, PaintData->WorldSize, Width, Height);
				if (BaseRect.Width() <= 0 || BaseRect.Height() <= 0) continue;
				LocalBefore.SetNumUninitialized(int64(BaseRect.Width()) * BaseRect.Height());
				for (int32 Y = BaseRect.Min.Y; Y < BaseRect.Max.Y; ++Y)
					FMemory::Memcpy(LocalBefore.GetData() + int64(Y - BaseRect.Min.Y) * BaseRect.Width(),
						Pixels.GetData() + int64(Y) * Width + BaseRect.Min.X, BaseRect.Width());
			}
			const TArray64<uint8>& Before = bCurrentStroke ? StrokeBefore : LocalBefore;
			for (int32 PointIndex = 0; PointIndex < Stroke.Points.Num(); ++PointIndex)
				RasterizeMaskSegment(Pixels, Before, BaseRect, *PaintData, Stroke,
					Stroke.Points[FMath::Max(PointIndex - 1, 0)], Stroke.Points[PointIndex]);
		}
	}

	void UploadPreview()
	{
		++MaskDebugRevision;
		if (!Preview || !Preview->GetResource()) return;
		// The render command owns its copy; never hand it the mutable CPU paint buffer.
		uint8* Upload = static_cast<uint8*>(FMemory::Malloc(Pixels.Num()));
		FMemory::Memcpy(Upload, Pixels.GetData(), Pixels.Num());
		auto* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Width, Height);
		Preview->UpdateTextureRegions(0, 1, Region, Width, 1, Upload,
			[](uint8* Data, const FUpdateTextureRegion2D* Regions)
			{
				FMemory::Free(Data);
				delete Regions;
			});
		if (UCloudSeaComponent* Target = GetTarget())
			Target->SetCloudMaskPaintPreview(Preview, PaintData->CenterWorldXY, PaintData->WorldSize);
		if (GEditor) GEditor->RedrawLevelEditingViewports();
	}

	void EndStroke()
	{
		if (!bInStroke) return;
		bInStroke = false;
		StrokeViewport = nullptr;
		if (bStrokeChanged)
		{
			// Recenter once on release unless the stroke outgrew the canvas earlier.
			if (FitCanvasToPaintedBounds())
			{
				RebuildMappedPixels();
				UploadPreview();
			}
			// Transactions target only the transient CPU canvas. The formal texture
			// remains untouched until the user explicitly accepts with Save Mask.
			PaintData->Pixels.Empty();
			PaintData->Pixels.Append(Pixels.GetData(), int32(Pixels.Num()));
		}
		else if (StrokeTransaction)
		{
			StrokeTransaction->Cancel();
		}
		StrokeTransaction.Reset();
		StrokeBefore.Empty();
	}

	using FEdMode::PostUndo;
	virtual void PostUndo(bool bSuccess) override { if (bSuccess) RefreshFromDraft(); }
	virtual void PostRedo(bool bSuccess) override { if (bSuccess) RefreshFromDraft(); }
	void RefreshFromDraft()
	{
		if (bInStroke) return;
		if (!PaintData || PaintData->Width != Width || PaintData->Height != Height
			|| PaintData->Pixels.Num() != Width * Height)
		{
			RequestDeletion();
			return;
		}
		Pixels.Empty();
		Pixels.Append(PaintData->Pixels.GetData(), PaintData->Pixels.Num());
		bMaskDebugFullRebuild = true;
		MaskDebugDirtyRect = FIntRect(0, 0, 0, 0);
		UploadPreview();
	}

	bool UpdateHover(FEditorViewportClient* Client)
	{
		bHasHover = false;
		UCloudSeaComponent* Target = GetTarget();
		if (!Target || Client->GetWorld() != Target->GetWorld()) return false;
		const FViewportCursorLocation Ray = Client->GetCursorWorldLocationFromMousePos();
		const FVector Direction = Ray.GetDirection();
		if (FMath::Abs(Direction.Z) < 1.e-6) return false;
		const double TopZ = Target->CloudLayerBaseWorldHeight + FMath::Max(Target->CloudLayerHeight, 0.001f) * 100.0;
		const double Distance = (TopZ - Ray.GetOrigin().Z) / Direction.Z;
		if (Distance < 0.0 || Distance > 1.e9) return false;
		Hover = Ray.GetOrigin() + Direction * Distance;
		HoverViewport = Client;
		bHasHover = true;
		return true;
	}

	// Record physical positions before rasterization so a growing canvas preserves
	// old holes in world space and redraws round brushes at their original size.
	void PaintSegment(const FVector2D& A, const FVector2D& B)
	{
		FCloudSeaMaskPaintStroke& Stroke = PaintData->Strokes.Last();
		if (Stroke.Points.IsEmpty()) Stroke.Points.Add(A);
		if (Stroke.Points.Last() != B) Stroke.Points.Add(B);
		const bool bHadBounds = PaintData->PaintedWorldBounds.bIsValid;
		const FVector2D Radius(FMath::Max(double(Stroke.RadiusMeters), 0.01) * 100.0);
		PaintData->PaintedWorldBounds += A - Radius;
		PaintData->PaintedWorldBounds += A + Radius;
		PaintData->PaintedWorldBounds += B - Radius;
		PaintData->PaintedWorldBounds += B + Radius;
		const FVector2D Half = PaintData->WorldSize * 50.0;
		const FBox2D& Bounds = PaintData->PaintedWorldBounds;
		const bool bOutside = Bounds.Min.X < PaintData->CenterWorldXY.X - Half.X || Bounds.Max.X > PaintData->CenterWorldXY.X + Half.X
			|| Bounds.Min.Y < PaintData->CenterWorldXY.Y - Half.Y || Bounds.Max.Y > PaintData->CenterWorldXY.Y + Half.Y;
		if ((!bHadBounds || bOutside) && FitCanvasToPaintedBounds()) RebuildMappedPixels();
		else
		{
			const FIntRect ChangedPixels = RasterizeMaskSegment(Pixels, StrokeBefore, FIntRect(0, 0, Width, Height), *PaintData, Stroke, A, B);
			if (ChangedPixels.Width() > 0 && ChangedPixels.Height() > 0)
			{
				if (MaskDebugDirtyRect.Width() > 0 && MaskDebugDirtyRect.Height() > 0) MaskDebugDirtyRect.Union(ChangedPixels);
				else MaskDebugDirtyRect = ChangedPixels;
			}
		}
		bStrokeChanged = true;
		UploadPreview();
	}

	virtual bool InputKey(FEditorViewportClient* Client, FViewport* Viewport, FKey Key, EInputEvent Event) override
	{
		if (Key == EKeys::Escape && Event == IE_Pressed)
		{
			EndStroke();
			if (ConfirmFinishMaskDraft()) RequestDeletion();
			return true;
		}
		if (bInStroke && Client->IsCtrlPressed()) EndStroke();
		if (Key != EKeys::LeftMouseButton) return false;
		if (Event == IE_Released && bInStroke)
		{
			EndStroke();
			return true;
		}
		if (Client->IsAltPressed() || Client->IsCtrlPressed() || Viewport->KeyState(EKeys::RightMouseButton)) return false;
		if (Event == IE_Pressed && UpdateHover(Client) && CanPaintComponent(GetTarget()))
		{
			EndStroke();
			if (BrushStrength <= 0.0f) return true;
			StrokeBefore = Pixels;
			StrokeTransaction = MakeUnique<FScopedTransaction>(LOCTEXT("Stroke", "Paint Cloud Sea Mask"));
			PaintData->Modify(false);
			bInStroke = true;
			StrokeViewport = Client;
			bStrokeChanged = false;
			bEraseStroke = bPaintWhite != Client->IsShiftPressed();
			FCloudSeaMaskPaintStroke& Stroke = PaintData->Strokes.AddDefaulted_GetRef();
			Stroke.RadiusMeters = BrushRadiusMeters;
			Stroke.Strength = BrushStrength;
			Stroke.Hardness = BrushHardness;
			Stroke.bPaintWhite = bEraseStroke;
			LastStrokePoint = FVector2D(Hover.X, Hover.Y);
			PaintSegment(LastStrokePoint, LastStrokePoint);
		}
		// Consume clicks in paint mode even when the cursor misses the plane.
		return true;
	}
	virtual bool MouseMove(FEditorViewportClient* Client, FViewport* Viewport, int32, int32) override
	{
		const bool bHitPlane = UpdateHover(Client);
		if (bInStroke && Client == StrokeViewport && !bHitPlane) EndStroke();
		if (bHitPlane && bInStroke && Client == StrokeViewport)
		{
			const FVector2D Point(Hover.X, Hover.Y);
			PaintSegment(LastStrokePoint, Point);
			LastStrokePoint = Point;
		}
		Client->Invalidate();
		return bInStroke;
	}
	virtual bool CapturedMouseMove(FEditorViewportClient* Client, FViewport* Viewport, int32 X, int32 Y) override
	{
		return MouseMove(Client, Viewport, X, Y);
	}
	virtual bool MouseLeave(FEditorViewportClient* Client, FViewport*) override
	{
		if (Client == StrokeViewport) EndStroke();
		bHasHover = false;
		return false;
	}
	virtual void Tick(FEditorViewportClient* Client, float DeltaTime) override
	{
		FEdMode::Tick(Client, DeltaTime);
		UCloudSeaComponent* Target = GetTarget();
		if (!CanPaintComponent(Target) || Target->CloudMaskTexture != Texture || !MaskDraft.HasOriginalMapping(Target))
		{
			EndStroke();
			RequestDeletion();
			return;
		}
		if (bInStroke && Client == StrokeViewport && (!Client->Viewport || !Client->Viewport->KeyState(EKeys::LeftMouseButton))) EndStroke();
		// Construction scripts can replace the component during a Details edit.
		Target->SetCloudMaskPaintPreview(Preview, PaintData->CenterWorldXY, PaintData->WorldSize);
	}
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override
	{
		UCloudSeaComponent* Target = GetTarget();
		if (!Target || !PaintData) return;
		const double Z = Target->CloudLayerBaseWorldHeight + FMath::Max(Target->CloudLayerHeight, 0.001f) * 100.0;
		const FVector Center(PaintData->CenterWorldXY.X, PaintData->CenterWorldXY.Y, Z);
		const FVector Half(PaintData->WorldSize.X * 50.0, PaintData->WorldSize.Y * 50.0, 0.0);
		const FVector Corners[] = { Center + FVector(-Half.X, -Half.Y, 0), Center + FVector(Half.X, -Half.Y, 0),
			Center + FVector(Half.X, Half.Y, 0), Center + FVector(-Half.X, Half.Y, 0) };
		if (!IsMaskDebugEnabled(Target))
			for (int32 I = 0; I < 4; ++I) PDI->DrawLine(Corners[I], Corners[(I + 1) % 4], FLinearColor::Yellow, SDPG_Foreground, 1.0f);
		// Painting must not depend on selection-driven component visualizers being dispatched.
		DrawActiveMaskDebug(Target, View, PDI);
		if (bHasHover && HoverViewport && HoverViewport->Viewport == Viewport)
		{
			const bool bShowingWhite = bInStroke ? bEraseStroke : (bPaintWhite != HoverViewport->IsShiftPressed());
			const FLinearColor Color = bShowingWhite ? FLinearColor::White : FLinearColor::Yellow;
			const auto DrawBrushRing = [PDI, this, Color](double Radius, float OutlineWidth, float ColorWidth)
			{
				// Screen-space line widths stay readable at any camera distance. A dark
				// outline keeps the bright ring visible on both white fog and dark cloud.
				DrawCircle(PDI, Hover, FVector::ForwardVector, FVector::RightVector, FLinearColor::Black,
					Radius, 128, SDPG_Foreground, OutlineWidth, 0.0f, true);
				DrawCircle(PDI, Hover, FVector::ForwardVector, FVector::RightVector, Color,
					Radius, 128, SDPG_Foreground, ColorWidth, 0.0f, true);
			};
			DrawBrushRing(BrushRadiusMeters * 100.0, 7.0f, 3.0f);
			if (BrushHardness > 0.0f && BrushHardness < 1.0f)
			{
				DrawBrushRing(BrushRadiusMeters * BrushHardness * 100.0, 4.0f, 1.5f);
			}
		}
	}
	virtual void DrawHUD(FEditorViewportClient* Client, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) override
	{
		FEdMode::DrawHUD(Client, Viewport, View, Canvas);
		DrawActiveMaskDebugHUD(GetTarget(), Viewport, View, Canvas);
	}
};

FCloudSeaMaskEdMode* ActivePainter()
{
	return GEditor ? GLevelEditorModeTools().GetActiveModeTyped<FCloudSeaMaskEdMode>(MaskModeId) : nullptr;
}

struct FMaskDebugEdge
{
	FIntPoint Start;
	FIntPoint End;
};

// Keep merged contours per grid line at native resolution. A changed pixel only
// affects its two horizontal and two vertical boundaries; rebuild those lines
// completely so merged runs remain identical to a full scan.
struct FMaskDebugEdges
{
	TArray<TArray<FMaskDebugEdge>> Rows;
	TArray<TArray<FMaskDebugEdge>> Columns;
	int32 NumEdges = 0;

	void Reset(int32 Width = 0, int32 Height = 0)
	{
		Rows.Empty();
		Columns.Empty();
		NumEdges = 0;
		if (Width > 0 && Height > 0)
		{
			Rows.SetNum(Height + 1);
			Columns.SetNum(Width + 1);
		}
	}

	bool IsEmpty() const { return NumEdges == 0; }

	void Update(const TArray64<uint8>& Pixels, int32 Width, int32 Height, const FIntRect& ChangedPixels)
	{
		check(Rows.Num() == Height + 1 && Columns.Num() == Width + 1);
		check(ChangedPixels.Min.X >= 0 && ChangedPixels.Min.Y >= 0
			&& ChangedPixels.Max.X <= Width && ChangedPixels.Max.Y <= Height);
		// Pixel rectangles have exclusive Max, but the boundary at Max also changes.
		for (int32 Y = ChangedPixels.Min.Y; Y <= ChangedPixels.Max.Y; ++Y)
		{
			TArray<FMaskDebugEdge>& Edges = Rows[Y];
			NumEdges -= Edges.Num();
			Edges.Reset();
			int32 RunStart = INDEX_NONE;
			for (int32 X = 0; X <= Width; ++X)
			{
				const bool bEdge = X < Width && ((Y > 0 && Pixels[int64(Y - 1) * Width + X] < 255)
					!= (Y < Height && Pixels[int64(Y) * Width + X] < 255));
				if (bEdge && RunStart == INDEX_NONE) RunStart = X;
				else if (!bEdge && RunStart != INDEX_NONE)
				{
					Edges.Add({ FIntPoint(RunStart, Y), FIntPoint(X, Y) });
					RunStart = INDEX_NONE;
				}
			}
			NumEdges += Edges.Num();
		}
		for (int32 X = ChangedPixels.Min.X; X <= ChangedPixels.Max.X; ++X)
		{
			TArray<FMaskDebugEdge>& Edges = Columns[X];
			NumEdges -= Edges.Num();
			Edges.Reset();
			int32 RunStart = INDEX_NONE;
			for (int32 Y = 0; Y <= Height; ++Y)
			{
				const bool bEdge = Y < Height && ((X > 0 && Pixels[int64(Y) * Width + X - 1] < 255)
					!= (X < Width && Pixels[int64(Y) * Width + X] < 255));
				if (bEdge && RunStart == INDEX_NONE) RunStart = Y;
				else if (!bEdge && RunStart != INDEX_NONE)
				{
					Edges.Add({ FIntPoint(X, RunStart), FIntPoint(X, Y) });
					RunStart = INDEX_NONE;
				}
			}
			NumEdges += Edges.Num();
		}
	}
};

class FCloudSeaMaskDebugVisualizer : public FComponentVisualizer
{
	TWeakObjectPtr<const UCloudSeaComponent> CachedTarget;
	TWeakObjectPtr<UTexture2D> CachedTexture;
	FGuid CachedSourceId;
	uint64 CachedRevision = MAX_uint64;
	bool bCachedDraft = false;
	bool bHasData = false;
	int32 Width = 0;
	int32 Height = 0;
	FMaskDebugEdges Edges;

	bool UpdateCache(const UCloudSeaComponent* Target, FVector2D& Center, FVector2D& Size)
	{
		FCloudSeaMaskEdMode* Mode = ActivePainter();
		const bool bDraft = Mode && Mode->GetTarget() == Target && Mode->PaintData;
		UTexture2D* Texture = bDraft ? Mode->Preview.Get() : Target->CloudMaskTexture.Get();
		const FGuid SourceId = !bDraft && Texture ? Texture->Source.GetId() : FGuid();
		Center = bDraft ? Mode->PaintData->CenterWorldXY : Target->CloudMaskCenterWorldXY;
		Size = bDraft ? Mode->PaintData->WorldSize : Target->CloudMaskWorldSize;
		const bool bSameSource = CachedTarget.Get() == Target && CachedTexture.Get() == Texture
			&& bCachedDraft == bDraft && CachedSourceId == SourceId;
		// Painting another component cannot invalidate an unchanged assigned texture.
		if (bSameSource && (!bDraft || CachedRevision == MaskDebugRevision)) return bHasData;
		const bool bUpdateDraft = bSameSource && bHasData && bDraft && !Mode->bMaskDebugFullRebuild
			&& Width == Mode->Width && Height == Mode->Height;
		CachedTarget = Target;
		CachedTexture = Texture;
		bCachedDraft = bDraft;
		CachedSourceId = SourceId;
		CachedRevision = MaskDebugRevision;
		bHasData = false;
		TArray64<uint8> SavedPixels;
		if (bDraft)
		{
			Width = Mode->Width;
			Height = Mode->Height;
			if (Width > 0 && Height > 0 && Mode->Pixels.Num() == int64(Width) * Height)
			{
				if (!bUpdateDraft)
				{
					Edges.Reset(Width, Height);
					Edges.Update(Mode->Pixels, Width, Height, FIntRect(0, 0, Width, Height));
				}
				else if (Mode->MaskDebugDirtyRect.Width() > 0 && Mode->MaskDebugDirtyRect.Height() > 0)
				{
					Edges.Update(Mode->Pixels, Width, Height, Mode->MaskDebugDirtyRect);
				}
				Mode->MaskDebugDirtyRect = FIntRect(0, 0, 0, 0);
				Mode->bMaskDebugFullRebuild = false;
				bHasData = true;
			}
		}
		else if (ReadMask(Texture, SavedPixels))
		{
			Width = int32(Texture->Source.GetSizeX());
			Height = int32(Texture->Source.GetSizeY());
			Edges.Reset(Width, Height);
			Edges.Update(SavedPixels, Width, Height, FIntRect(0, 0, Width, Height));
			bHasData = true;
		}
		if (!bHasData) Edges.Reset();
		return bHasData;
	}

public:
	virtual void DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI) override
	{
		const UCloudSeaComponent* Target = Cast<UCloudSeaComponent>(Component);
		// The active paint mode draws this target directly; do not draw the same lines twice.
		const FCloudSeaMaskEdMode* Mode = ActivePainter();
		if (Mode && Mode->GetTarget() == Target) return;
		DrawMask(Target, View, PDI);
	}

	virtual void DrawVisualizationHUD(const UActorComponent* Component, const FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) override
	{
		const UCloudSeaComponent* Target = Cast<UCloudSeaComponent>(Component);
		const FCloudSeaMaskEdMode* Mode = ActivePainter();
		// Mode HUDs are dispatched only while ModeWidgets is enabled. Otherwise retain
		// this selection-driven HUD when the viewport still allows component visualizers.
		if (Mode && Mode->GetTarget() == Target && View->Family->EngineShowFlags.ModeWidgets) return;
		DrawMaskHUD(Target, Viewport, View, Canvas);
	}

	void DrawMask(const UCloudSeaComponent* Target, const FSceneView* View, FPrimitiveDrawInterface* PDI)
	{
		if (!IsMaskDebugEnabled(Target) || !CanPaintComponent(Target) || PDI->IsHitTesting()) return;
		FVector2D Center, Size;
		if (!UpdateCache(Target, Center, Size)) return;
		const double Z = Target->CloudLayerBaseWorldHeight + FMath::Max(Target->CloudLayerHeight, 0.001f) * 100.0;
		const FVector2D Corner = Center - Size * 50.0;
		const auto WorldPoint = [Corner, Size, Z, this](const FIntPoint& Pixel)
		{
			return FVector(Corner.X + double(Pixel.X) / Width * Size.X * 100.0,
				Corner.Y + double(Pixel.Y) / Height * Size.Y * 100.0, Z);
		};
		const auto DrawEdge = [PDI](const FVector& A, const FVector& B, const FLinearColor& Color)
		{
			PDI->DrawLine(A, B, FLinearColor::Black, SDPG_Foreground, 5.0f, 0.0f, true);
			PDI->DrawLine(A, B, Color, SDPG_Foreground, 2.0f, 0.0f, true);
		};
		for (const TArray<FMaskDebugEdge>& Row : Edges.Rows)
			for (const FMaskDebugEdge& Edge : Row)
				DrawEdge(WorldPoint(Edge.Start), WorldPoint(Edge.End), FLinearColor(1.0f, 0.12f, 0.05f));
		for (const TArray<FMaskDebugEdge>& Column : Edges.Columns)
			for (const FMaskDebugEdge& Edge : Column)
				DrawEdge(WorldPoint(Edge.Start), WorldPoint(Edge.End), FLinearColor(1.0f, 0.12f, 0.05f));
		const FIntPoint Corners[] = { {0, 0}, {Width, 0}, {Width, Height}, {0, Height} };
		for (int32 I = 0; I < 4; ++I) DrawEdge(WorldPoint(Corners[I]), WorldPoint(Corners[(I + 1) % 4]), FLinearColor::Yellow);
		const double CrossRadius = FMath::Clamp(FMath::Min(Size.X, Size.Y) * 0.5, 50.0, 2000.0);
		DrawEdge(FVector(Center.X - CrossRadius, Center.Y, Z), FVector(Center.X + CrossRadius, Center.Y, Z), FLinearColor(0, 1, 1));
		DrawEdge(FVector(Center.X, Center.Y - CrossRadius, Z), FVector(Center.X, Center.Y + CrossRadius, Z), FLinearColor(0, 1, 1));
	}

	void DrawMaskHUD(const UCloudSeaComponent* Target, const FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
	{
		if (!IsMaskDebugEnabled(Target) || !CanPaintComponent(Target) || !Viewport || !Canvas || !GEngine) return;
		FVector2D Center, Size;
		const bool bValid = UpdateCache(Target, Center, Size);
		const FText Message = !bValid ? LOCTEXT("DebugNoMask", "MASK DEBUG: No readable mask. Start or resume a draft to inspect it.") : FText::Format(
			LOCTEXT("DebugLegend", "MASK DEBUG ON [{0}]\nRed: masked area boundaries (includes gray) | Yellow: coverage | Cyan: center. {1}"),
			bCachedDraft ? LOCTEXT("DebugDraft", "Draft") : LOCTEXT("DebugSaved", "Assigned texture"),
			Edges.IsEmpty() ? LOCTEXT("DebugWhite", "All white: no masked area.") : FText::GetEmpty());
		const float TextY = FMath::Max(48.0f, float(Viewport->GetSizeXY().Y) - 64.0f);
		FCanvasTextItem Text(FVector2D(20.0, TextY), Message, GEngine->GetSmallFont(), FLinearColor::White);
		Text.EnableShadow(FLinearColor::Black);
		Text.Draw(Canvas);
	}
};

TSharedPtr<FCloudSeaMaskDebugVisualizer> MaskDebugVisualizer;

TSharedRef<FCloudSeaMaskDebugVisualizer> GetMaskDebugVisualizer()
{
	if (!MaskDebugVisualizer) MaskDebugVisualizer = MakeShared<FCloudSeaMaskDebugVisualizer>();
	return MaskDebugVisualizer.ToSharedRef();
}

void DrawActiveMaskDebug(const UCloudSeaComponent* Target, const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	if (IsMaskDebugEnabled(Target)) GetMaskDebugVisualizer()->DrawMask(Target, View, PDI);
}

void DrawActiveMaskDebugHUD(const UCloudSeaComponent* Target, const FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	if (IsMaskDebugEnabled(Target)) GetMaskDebugVisualizer()->DrawMaskHUD(Target, Viewport, View, Canvas);
}

void RegisterMaskDebugVisualizer()
{
	if (GUnrealEd && !bMaskDebugRegistered)
	{
		TSharedRef<FCloudSeaMaskDebugVisualizer> Visualizer = GetMaskDebugVisualizer();
		GUnrealEd->RegisterComponentVisualizer(UCloudSeaComponent::StaticClass()->GetFName(), Visualizer);
		Visualizer->OnRegister();
		bMaskDebugRegistered = true;
	}
}

void DiscardMaskDraft()
{
	if (FCloudSeaMaskEdMode* Mode = ActivePainter()) Mode->EndStroke();
	if (UCloudSeaComponent* Target = MaskDraft.GetTarget()) Target->SetCloudMaskPaintPreview(nullptr);
	MaskDraft.Reset();
	if (GEditor) GEditor->RedrawLevelEditingViewports();
}

bool CommitMaskDraft()
{
	if (FCloudSeaMaskEdMode* Mode = ActivePainter()) Mode->EndStroke();
	UCloudSeaMaskPaintData* Data = MaskDraft.Data.Get();
	if (!Data || Data->Width <= 0 || Data->Height <= 0 || Data->Pixels.Num() != Data->Width * Data->Height) return false;
	UCloudSeaComponent* Target = MaskDraft.GetTarget();
	if (!IsValid(Target) || Target->CloudMaskTexture.Get() != MaskDraft.OriginalAssignedTexture.Get() || !MaskDraft.HasOriginalMapping(Target))
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("DraftTargetChanged", "The draft's component is unavailable, or its texture, center or world size has changed. Restore its original settings to continue, or discard this draft."));
		return false;
	}

	// This is the only path that creates a formal asset or writes its source pixels.
	bool bWasDirty = false;
	if (!MaskDraft.Texture.Get())
	{
		FSaveAssetDialogConfig Config;
		Config.DialogTitleOverride = LOCTEXT("SaveDraftTitle", "Save Cloud Sea Mask");
		Config.DefaultPath = TEXT("/CloudSea/Textures");
		Config.DefaultAssetName = TEXT("T_CloudSeaMask");
		Config.AssetClassNames.Add(UTexture2D::StaticClass()->GetClassPathName());
		Config.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::Disallow;
		const FString ObjectPath = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get().CreateModalSaveAssetDialog(Config);
		if (ObjectPath.IsEmpty()) return false;
		UPackage* Package = CreatePackage(*FPackageName::ObjectPathToPackageName(ObjectPath));
		Package->FullyLoad();
		const FString Name = FPackageName::ObjectPathToObjectName(ObjectPath);
		if (FindObject<UObject>(Package, *Name)) return false;
		bWasDirty = Package->IsDirty();
		UTexture2D* NewTexture = NewObject<UTexture2D>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
		NewTexture->SRGB = false;
		NewTexture->CompressionSettings = TC_BC7;
		NewTexture->NeverStream = true;
		NewTexture->Filter = TF_Bilinear;
		NewTexture->AddressX = TA_Clamp;
		NewTexture->AddressY = TA_Clamp;
		MaskDraft.Texture.Reset(NewTexture);
	}
	else
	{
		bWasDirty = MaskDraft.Texture->GetOutermost()->IsDirty();
	}

	UTexture2D* Texture = MaskDraft.Texture.Get();
	const TextureMipGenSettings PreviousMipGenSettings = Texture->MipGenSettings;
	TArray64<uint8> PreviousPixels;
	int32 PreviousWidth = 0;
	int32 PreviousHeight = 0;
	if (!MaskDraft.bNewTexture)
	{
		if (!ReadMask(Texture, PreviousPixels)) return false;
		PreviousWidth = Texture->Source.GetSizeX();
		PreviousHeight = Texture->Source.GetSizeY();
		const bool bSourceChanged = PreviousWidth != Data->Width || PreviousHeight != Data->Height
			|| PreviousPixels.Num() != MaskDraft.OriginalPixels.Num()
			|| FMemory::Memcmp(PreviousPixels.GetData(), MaskDraft.OriginalPixels.GetData(), PreviousPixels.Num()) != 0;
		if (bSourceChanged && FMessageDialog::Open(EAppMsgType::YesNo, EAppReturnType::No,
			LOCTEXT("SourceChanged", "The texture changed since this draft started. Replace its current pixels with this draft?")) != EAppReturnType::Yes) return false;
	}

	FScopedTransaction Transaction(LOCTEXT("AcceptDraft", "Apply Cloud Sea Mask Draft"));
	Texture->SetFlags(RF_Transactional);
	Texture->Modify();
	Texture->PreEditChange(nullptr);
	// Store only the painted source mip; let the texture build generate the mip chain.
	Texture->MipGenSettings = TMGS_SimpleAverage;
	Texture->Source.Init(Data->Width, Data->Height, 1, 1, TSF_G8, Data->Pixels.GetData());
	Texture->PostEditChange();
	if (MaskDraft.bNewTexture) FAssetRegistryModule::AssetCreated(Texture);
	Texture->MarkPackageDirty();
	const FEditorFileUtils::EPromptReturnCode Result = FEditorFileUtils::PromptForCheckoutAndSave(
		{ Texture->GetOutermost() }, true, false);
	if (Result != FEditorFileUtils::PR_Success)
	{
		Transaction.Cancel();
		// Cancelling checkout/save must not leave an existing texture with accepted
		// draft pixels in memory, where Save All could later persist them accidentally.
		if (!MaskDraft.bNewTexture)
		{
			Texture->PreEditChange(nullptr);
			Texture->MipGenSettings = PreviousMipGenSettings;
			Texture->Source.Init(PreviousWidth, PreviousHeight, 1, 1, TSF_G8, PreviousPixels.GetData());
			Texture->PostEditChange();
			Texture->GetOutermost()->SetDirtyFlag(bWasDirty);
		}
		else
		{
			// A cancelled new save must not leave a blank/partial asset for Save All.
			// Keep the CPU draft; only retire the newly created in-memory asset.
			UTexture* Textures[] = { Texture };
			FTextureCompilingManager::Get().FinishCompilation(Textures);
			UPackage* Package = Texture->GetOutermost();
			FAssetRegistryModule::AssetDeleted(Texture);
			Texture->ClearFlags(RF_Public | RF_Standalone | RF_Transactional);
			Texture->SetFlags(RF_Transient);
			const FName RetiredName = MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass());
			Texture->Rename(*RetiredName.ToString(), GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
			Package->SetDirtyFlag(bWasDirty);
			MaskDraft.Texture.Reset();
		}
		return false;
	}

	if (UCloudSeaComponent* CurrentTarget = MaskDraft.GetTarget())
	{
		// Pixel coordinates and world mapping must be accepted together. Keep editor
		// previews out of the actor transaction, including construction-script components.
		CurrentTarget->SetCloudMaskPaintPreview(nullptr);
		CurrentTarget->Modify();
		CurrentTarget->PreEditChange(nullptr);
		if (MaskDraft.bNewTexture) CurrentTarget->CloudMaskTexture = Texture;
		CurrentTarget->CloudMaskCenterWorldXY = Data->CenterWorldXY;
		CurrentTarget->CloudMaskWorldSize = Data->WorldSize;
		FPropertyChangedEvent Event(nullptr, EPropertyChangeType::ValueSet);
		CurrentTarget->PostEditChangeProperty(Event); // May reconstruct the component.
	}
	DiscardMaskDraft();
	return true;
}

bool ConfirmFinishMaskDraft()
{
	if (FCloudSeaMaskEdMode* Mode = ActivePainter()) Mode->EndStroke();
	if (!MaskDraft.IsModified())
	{
		DiscardMaskDraft();
		return true;
	}
	const EAppReturnType::Type Choice = FMessageDialog::Open(EAppMsgType::YesNoCancel, EAppReturnType::Cancel,
		LOCTEXT("FinishDraft", "Save this mask draft before stopping?\nYes: save the texture. No: discard the draft. Cancel: continue painting."));
	if (Choice == EAppReturnType::Yes) return CommitMaskDraft();
	if (Choice == EAppReturnType::No)
	{
		DiscardMaskDraft();
		return true;
	}
	return false;
}

bool BeginMaskDraft(UCloudSeaComponent* Target, bool bNewTexture)
{
	if (!CanPaintComponent(Target)) return false;
	const TWeakObjectPtr<AActor> RequestedActor = Target->GetOwner();
	const FName RequestedComponentName = Target->GetFName();
	if (ActivePainter())
	{
		if (!bNewTexture && MaskDraft.Matches(Target)) return true;
		if (!ConfirmFinishMaskDraft()) return false;
		GLevelEditorModeTools().DeactivateMode(MaskModeId);
	}
	if (MaskDraft.Data.Get() && (bNewTexture || !MaskDraft.Matches(Target)))
	{
		if (!ConfirmFinishMaskDraft()) return false;
	}
	// Accepting the previous draft can reconstruct this actor's components.
	Target = FindMaskComponent(RequestedActor.Get(), RequestedComponentName);
	if (!CanPaintComponent(Target)) return false;
	if (MaskDraft.Data.Get() && (Target->CloudMaskTexture.Get() != MaskDraft.OriginalAssignedTexture.Get() || !MaskDraft.HasOriginalMapping(Target)))
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("DraftAssignmentChanged", "The assigned texture, center or world size has changed. Restore the original settings to resume, or discard this draft before starting again."));
		return false;
	}
	if (!MaskDraft.Data.Get())
	{
		TArray64<uint8> SourcePixels;
		if (!bNewTexture && !ReadMask(Target->CloudMaskTexture, SourcePixels))
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("InvalidDraftSource", "Select a linear G8 mask, or use New Mask Draft to paint a new texture."));
			return false;
		}
		MaskDraft.Data.Reset(NewObject<UCloudSeaMaskPaintData>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional));
		MaskDraft.Texture.Reset(bNewTexture ? nullptr : Target->CloudMaskTexture.Get());
		MaskDraft.OriginalAssignedTexture = Target->CloudMaskTexture.Get();
		MaskDraft.Actor = Target->GetOwner();
		MaskDraft.ComponentName = Target->GetFName();
		MaskDraft.bNewTexture = bNewTexture;
		MaskDraft.OriginalCenter = Target->CloudMaskCenterWorldXY;
		MaskDraft.OriginalWorldSize = Target->CloudMaskWorldSize;
		MaskDraft.Data->Width = bNewTexture ? NewMaskResolution : Target->CloudMaskTexture->Source.GetSizeX();
		MaskDraft.Data->Height = bNewTexture ? NewMaskResolution : Target->CloudMaskTexture->Source.GetSizeY();
		MaskDraft.Data->CenterWorldXY = Target->CloudMaskCenterWorldXY;
		MaskDraft.Data->WorldSize = FVector2D(FMath::Max(Target->CloudMaskWorldSize.X, 1.0), FMath::Max(Target->CloudMaskWorldSize.Y, 1.0));
		MaskDraft.Data->bUseOriginalPixels = !bNewTexture;
		if (bNewTexture) MaskDraft.Data->Pixels.Init(255, NewMaskResolution * NewMaskResolution);
		else MaskDraft.Data->Pixels.Append(SourcePixels.GetData(), int32(SourcePixels.Num()));
		MaskDraft.OriginalPixels = MaskDraft.Data->Pixels;
		MaskDraft.Data->PaintedWorldBounds = bNewTexture ? FBox2D(ForceInit) : OriginalMaskBounds(*MaskDraft.Data);
	}
	GLevelEditorModeTools().ActivateMode(MaskModeId);
	FCloudSeaMaskEdMode* Mode = ActivePainter();
	if (!Mode || !Mode->Start(Target))
	{
		GLevelEditorModeTools().DeactivateMode(MaskModeId);
		return false; // The draft remains available to resume.
	}
	return true;
}

void ResetMaskToWhite(UCloudSeaComponent* Target)
{
	if (!BeginMaskDraft(Target, false)) return;
	FCloudSeaMaskEdMode* Mode = ActivePainter();
	Mode->EndStroke();
	UCloudSeaMaskPaintData* Data = MaskDraft.Data.Get();
	bool bAlreadyWhite = true;
	for (uint8 Value : Data->Pixels) if (Value != 255) { bAlreadyWhite = false; break; }
	if (bAlreadyWhite && Data->Strokes.IsEmpty() && !Data->bUseOriginalPixels && !Data->PaintedWorldBounds.bIsValid) return;
	{
		const FScopedTransaction Transaction(LOCTEXT("ResetWhiteTransaction", "Reset Cloud Sea Mask Draft to White"));
		Data->Modify(false);
		Data->Pixels.Init(255, Data->Width * Data->Height);
		Data->Strokes.Empty();
		Data->bUseOriginalPixels = false;
		Data->PaintedWorldBounds.Init();
	}
	Mode->RefreshFromDraft();
}

class FCloudSeaMaskDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance() { return MakeShared<FCloudSeaMaskDetails>(); }
	virtual void CustomizeDetails(IDetailLayoutBuilder& Builder) override
	{
		TArray<TWeakObjectPtr<UObject>> Objects;
		Builder.GetObjectsBeingCustomized(Objects);
		if (Objects.Num() != 1) return;
		TWeakObjectPtr<UCloudSeaComponent> Target = Cast<UCloudSeaComponent>(Objects[0].Get());
		if (!Target.IsValid()) return;
		IDetailCategoryBuilder& Category = Builder.EditCategory("Cloud Sea|Mask");
		Category.AddProperty(Builder.GetProperty(GET_MEMBER_NAME_CHECKED(UCloudSeaComponent, bEnableCloudMask)));
		Category.AddCustomRow(LOCTEXT("DebugMask", "Debug Mask"))
		.NameContent()[SNew(STextBlock).Text(LOCTEXT("DebugMask", "Debug Mask"))]
		.ValueContent()
		[
			SNew(SCheckBox)
			.IsEnabled_Lambda([Target]() { return CanPaintComponent(Target.Get()) || IsMaskDebugEnabled(Target.Get()); })
			.IsChecked_Lambda([Target]() { return IsMaskDebugEnabled(Target.Get()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.ToolTipText(LOCTEXT("DebugMaskTip", "While painting, the paint mode directly shows the live draft. When stopped, keep this cloud selected with Game View / G off to inspect the assigned texture. Red contours mark every non-white mask texel, including gray; yellow is coverage and cyan is center. Does not modify the mask."))
			.OnCheckStateChanged_Lambda([Target](ECheckBoxState State)
			{
				if (State == ECheckBoxState::Checked && Target.IsValid())
				{
					MaskDebugOwner = Target->GetOwner();
					MaskDebugComponentName = Target->GetFName();
				}
				else
				{
					MaskDebugOwner.Reset();
					MaskDebugComponentName = NAME_None;
				}
				++MaskDebugRevision;
				if (GEditor) GEditor->RedrawLevelEditingViewports();
			})
		];
		Category.AddCustomRow(LOCTEXT("HelpFilter", "Paint Mask")).WholeRowContent()
		[
			SNew(STextBlock).AutoWrapText(true).Text_Lambda([Target]()
			{
				if (Target.IsValid() && Target->VerticalRenderingMode != ECloudSeaVerticalRenderingMode::SingleLayerWithoutPrefill)
					return LOCTEXT("MirrorDisabled", "Mask only works in Single Layer without Prefill. Mirror mode ignores the mask.");
				return LOCTEXT("Help", "Paint on the cloud-top plane. The yellow rectangle automatically centers on the painted area and grows as needed; texture resolution stays fixed. Paint White off: remove cloud; on: restore cloud. Shift reverses. Ctrl+Z undoes pixels and mapping together. Save Mask accepts the draft. Then save the actor/level to keep its center and world size.");
			})
		];
		Category.AddCustomRow(LOCTEXT("DraftStatus", "Mask Draft Status")).WholeRowContent()
		[
			SNew(STextBlock).AutoWrapText(true).Text_Lambda([Target]()
			{
				if (!MaskDraft.Matches(Target.Get())) return LOCTEXT("NoDraft", "No active draft. New Mask Draft starts with white; Start Painting edits the assigned texture.");
				const UCloudSeaMaskPaintData* Data = MaskDraft.Data.Get();
				return FText::Format(LOCTEXT("DraftPending", "Draft center (cm): {0}, {1} | Size (m): {2} x {3}. Texture and component settings remain unchanged until Save Mask. Drafts stay in memory; save before closing the editor."),
					FText::AsNumber(Data->CenterWorldXY.X), FText::AsNumber(Data->CenterWorldXY.Y),
					FText::AsNumber(Data->WorldSize.X), FText::AsNumber(Data->WorldSize.Y));
			})
		];
		Category.AddCustomRow(LOCTEXT("Resolution", "New Mask Resolution"))
		.NameContent()[SNew(STextBlock).Text(LOCTEXT("Resolution", "New Mask Resolution"))]
		.ValueContent().MinDesiredWidth(240)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[ResolutionButton(512)]
			+ SHorizontalBox::Slot().AutoWidth()[ResolutionButton(1024)]
			+ SHorizontalBox::Slot().AutoWidth()[ResolutionButton(2048)]
		];
		Category.AddCustomRow(LOCTEXT("Actions", "Create Paint Save")) .WholeRowContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SButton).Text(LOCTEXT("NewDraft", "New Mask Draft"))
				.IsEnabled_Lambda([Target]() { return CanPaintComponent(Target.Get()) && !ActivePainter(); })
				.OnClicked_Lambda([Target]() { if (Target.IsValid()) BeginMaskDraft(Target.Get(), true); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SButton).Text_Lambda([Target]() { return ActivePainter() ? LOCTEXT("Stop", "Stop Painting") : (MaskDraft.Matches(Target.Get()) ? LOCTEXT("Resume", "Resume Draft") : LOCTEXT("Paint", "Start Painting")); })
				.IsEnabled_Lambda([Target]() { return ActivePainter() || (CanPaintComponent(Target.Get()) && (MaskDraft.Matches(Target.Get()) || Target->CloudMaskTexture)); })
				.OnClicked_Lambda([Target]()
				{
					if (ActivePainter())
					{
						if (ConfirmFinishMaskDraft()) GLevelEditorModeTools().DeactivateMode(MaskModeId);
					}
					else if (Target.IsValid())
					{
						BeginMaskDraft(Target.Get(), false);
					}
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SButton).Text(LOCTEXT("Save", "Save Mask"))
				.IsEnabled_Lambda([Target]() { return MaskDraft.Matches(Target.Get()) && GEditor && !GEditor->PlayWorld; })
				.OnClicked_Lambda([]() { if (CommitMaskDraft() && ActivePainter()) GLevelEditorModeTools().DeactivateMode(MaskModeId); return FReply::Handled(); })
			]
		];
		Category.AddCustomRow(LOCTEXT("ResetWhite", "Reset Mask to White")).WholeRowContent()
		[
			SNew(SButton).Text(LOCTEXT("ResetWhite", "Reset Mask to White"))
			.ToolTipText(LOCTEXT("ResetWhiteTip", "Fill the draft with white. Ctrl+Z undoes the reset. The formal texture is changed only by Save Mask."))
			.IsEnabled_Lambda([Target]() { return CanPaintComponent(Target.Get()) && (MaskDraft.Matches(Target.Get()) || Target->CloudMaskTexture); })
			.OnClicked_Lambda([Target]() { if (Target.IsValid()) ResetMaskToWhite(Target.Get()); return FReply::Handled(); })
		];
		Category.AddCustomRow(LOCTEXT("Discard", "Discard Changes")).WholeRowContent()
		[
			SNew(SButton).Text(LOCTEXT("Discard", "Discard Changes"))
			.ToolTipText(LOCTEXT("DiscardTip", "Discard the in-memory draft and return to the assigned texture. No texture asset is saved."))
			.IsEnabled_Lambda([Target]() { return MaskDraft.Matches(Target.Get()) && GEditor && !GEditor->PlayWorld; })
			.OnClicked_Lambda([]() { DiscardMaskDraft(); if (ActivePainter()) GLevelEditorModeTools().DeactivateMode(MaskModeId); return FReply::Handled(); })
		];
		AddBrushNumber(Category, LOCTEXT("Radius", "Brush Radius (m)"), &BrushRadiusMeters, 0.1f, 5000.0f);
		AddBrushNumber(Category, LOCTEXT("Strength", "Brush Strength"), &BrushStrength, 0.0f, 1.0f);
		AddBrushNumber(Category, LOCTEXT("Hardness", "Brush Hardness"), &BrushHardness, 0.0f, 1.0f);
		Category.AddCustomRow(LOCTEXT("PaintWhite", "Paint White"))
		.NameContent()[SNew(STextBlock).Text(LOCTEXT("PaintWhite", "Paint White"))]
		.ValueContent()
		[
			SNew(SCheckBox).IsChecked_Lambda([]() { return bPaintWhite ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.ToolTipText(LOCTEXT("PaintWhiteTip", "Checked: paint white to restore cloud. Unchecked: paint black to remove cloud. Hold Shift to temporarily reverse."))
			.OnCheckStateChanged_Lambda([](ECheckBoxState State) { bPaintWhite = State == ECheckBoxState::Checked; })
		];
	}

	static TSharedRef<SWidget> ResolutionButton(int32 Resolution)
	{
		return SNew(SCheckBox).Style(FAppStyle::Get(), "ToggleButtonCheckbox")
			.IsChecked_Lambda([Resolution]() { return NewMaskResolution == Resolution ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([Resolution](ECheckBoxState) { NewMaskResolution = Resolution; })
			[SNew(STextBlock).Text(FText::AsNumber(Resolution))];
	}
	static void AddBrushNumber(IDetailCategoryBuilder& Category, FText Label, float* Value, float Min, float Max)
	{
		Category.AddCustomRow(Label).NameContent()[SNew(STextBlock).Text(Label)]
		.ValueContent().MinDesiredWidth(180)
		[
			SNew(SNumericEntryBox<float>).AllowSpin(true).MinValue(Min).MaxValue(Max).MinSliderValue(Min).MaxSliderValue(Max)
			.Value_Lambda([Value]() { return TOptional<float>(*Value); })
			.OnValueChanged_Lambda([Value, Min, Max](float V) { *Value = FMath::Clamp(V, Min, Max); })
		];
	}
};
}

void RegisterCloudSeaMaskPainter()
{
	if (GUnrealEd) RegisterMaskDebugVisualizer();
	else MaskDebugRegisterHandle = FCoreDelegates::OnPostEngineInit.AddStatic(&RegisterMaskDebugVisualizer);
	FEditorModeRegistry::Get().RegisterMode<FCloudSeaMaskEdMode>(MaskModeId, LOCTEXT("ModeName", "Cloud Sea Mask"));
	FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor").RegisterCustomClassLayout(
		"CloudSeaComponent", FOnGetDetailCustomizationInstance::CreateStatic(&FCloudSeaMaskDetails::MakeInstance));
}

void UnregisterCloudSeaMaskPainter()
{
	FCoreDelegates::OnPostEngineInit.Remove(MaskDebugRegisterHandle);
	MaskDebugRegisterHandle.Reset();
	if (GUnrealEd && bMaskDebugRegistered) GUnrealEd->UnregisterComponentVisualizer(UCloudSeaComponent::StaticClass()->GetFName());
	bMaskDebugRegistered = false;
	MaskDebugOwner.Reset();
	MaskDebugComponentName = NAME_None;
	if (ActivePainter()) GLevelEditorModeTools().DeactivateMode(MaskModeId);
	MaskDraft.Reset();
	MaskDebugVisualizer.Reset();
	FEditorModeRegistry::Get().UnregisterMode(MaskModeId);
	if (FPropertyEditorModule* Module = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor"))
	{
		Module->UnregisterCustomClassLayout("CloudSeaComponent");
	}
}

#undef LOCTEXT_NAMESPACE
