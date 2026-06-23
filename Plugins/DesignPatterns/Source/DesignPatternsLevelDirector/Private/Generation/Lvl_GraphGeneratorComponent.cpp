// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Generation/Lvl_GraphGeneratorComponent.h"
#include "Generation/Lvl_DungeonGraphRuleSet.h"
#include "Placement/Lvl_ProceduralPlacerComponent.h"
#include "Save/Lvl_SaveGameRegenHelpers.h"
#include "DesignPatternsLevelDirectorNativeTags.h"
#include "Lvl_BusPayloads.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"

// FInstancedStruct lives in StructUtils on 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

namespace
{
	/** Cardinal neighbour offsets in ELvl_TileConnector order: North, East, South, West. */
	static const FIntPoint GCardinals[4] = {
		FIntPoint(0, 1),   // North (+Y)
		FIntPoint(1, 0),   // East  (+X)
		FIntPoint(0, -1),  // South (-Y)
		FIntPoint(-1, 0)   // West  (-X)
	};

	/** The connector bit a tile must expose to open toward GCardinals[Direction]. */
	static ELvl_TileConnector ConnectorForDirection(int32 Direction)
	{
		switch (Direction)
		{
		case 0: return ELvl_TileConnector::North;
		case 1: return ELvl_TileConnector::East;
		case 2: return ELvl_TileConnector::South;
		default: return ELvl_TileConnector::West;
		}
	}
}

ULvl_GraphGeneratorComponent::ULvl_GraphGeneratorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	// Generation is authority-only; the component itself is not replicated (spawned actors are).
	SetIsReplicatedByDefault(false);
}

void ULvl_GraphGeneratorComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!TargetPlacer)
	{
		TargetPlacer = ResolveTargetPlacer();
	}

	if (bGenerateOnBeginPlay && HasWorldAuthority())
	{
		GenerateGraph();
	}
}

void ULvl_GraphGeneratorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bClearOnEndPlay && HasWorldAuthority())
	{
		if (ULvl_ProceduralPlacerComponent* Placer = ResolveTargetPlacer())
		{
			Placer->ClearPlacement();
		}
		GraphManifest.Reset();
	}
	Super::EndPlay(EndPlayReason);
}

bool ULvl_GraphGeneratorComponent::HasWorldAuthority() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

ULvl_ProceduralPlacerComponent* ULvl_GraphGeneratorComponent::ResolveTargetPlacer() const
{
	if (TargetPlacer)
	{
		return TargetPlacer;
	}
	if (const AActor* Owner = GetOwner())
	{
		return Owner->FindComponentByClass<ULvl_ProceduralPlacerComponent>();
	}
	return nullptr;
}

const FIntPoint& ULvl_GraphGeneratorComponent::CardinalOffset(int32 Direction)
{
	return GCardinals[FMath::Clamp(Direction, 0, 3)];
}

int32 ULvl_GraphGeneratorComponent::GetEffectiveSeed() const
{
	if (SeedOverride >= 0)
	{
		return SeedOverride;
	}
	if (GraphRuleSet && GraphRuleSet->RandomSeed != 0)
	{
		return GraphRuleSet->RandomSeed;
	}
	const AActor* Owner = GetOwner();
	const FString Key = Owner ? Owner->GetName() : GetName();
	return static_cast<int32>(GetTypeHash(Key) & 0x7fffffff);
}

FGameplayTag ULvl_GraphGeneratorComponent::GetEffectiveRegionTag() const
{
	if (RegionTagOverride.IsValid())
	{
		return RegionTagOverride;
	}
	return GraphRuleSet ? GraphRuleSet->DefaultRegionTag : FGameplayTag();
}

//~ Generation -------------------------------------------------------------------------------------

void ULvl_GraphGeneratorComponent::BuildRoomGraph(FRandomStream& Stream, TArray<FLvl_RoomNode>& OutRooms,
	TArray<FLvl_CorridorEdge>& OutEdges) const
{
	OutRooms.Reset();
	OutEdges.Reset();
	if (!GraphRuleSet)
	{
		return;
	}

	const FIntPoint Grid = FIntPoint(FMath::Max(1, GraphRuleSet->GridDimensions.X),
		FMath::Max(1, GraphRuleSet->GridDimensions.Y));
	const int32 TargetRooms = GraphRuleSet->GetEffectiveRoomCount(Stream);
	const int32 Attempts = FMath::Max(1, GraphRuleSet->RoomPlacementAttempts);

	// Grid-pack non-overlapping rooms with a 1-cell margin so corridors can run between them.
	for (int32 RoomIndex = 0; RoomIndex < TargetRooms; ++RoomIndex)
	{
		FIntPoint Size = GraphRuleSet->SampleRoomSize(Stream);
		Size.X = FMath::Min(Size.X, Grid.X);
		Size.Y = FMath::Min(Size.Y, Grid.Y);

		bool bPlaced = false;
		for (int32 Attempt = 0; Attempt < Attempts && !bPlaced; ++Attempt)
		{
			const int32 MaxOX = FMath::Max(0, Grid.X - Size.X);
			const int32 MaxOY = FMath::Max(0, Grid.Y - Size.Y);
			const FIntPoint Origin(Stream.RandRange(0, MaxOX), Stream.RandRange(0, MaxOY));

			FLvl_RoomNode Candidate;
			Candidate.OriginCell = Origin;
			Candidate.SizeCells = Size;

			// Overlap test with a 1-cell padding.
			bool bOverlaps = false;
			for (const FLvl_RoomNode& Existing : OutRooms)
			{
				const bool bSepX = (Origin.X + Size.X + 1 <= Existing.OriginCell.X)
					|| (Existing.OriginCell.X + Existing.SizeCells.X + 1 <= Origin.X);
				const bool bSepY = (Origin.Y + Size.Y + 1 <= Existing.OriginCell.Y)
					|| (Existing.OriginCell.Y + Existing.SizeCells.Y + 1 <= Origin.Y);
				if (!bSepX && !bSepY)
				{
					bOverlaps = true;
					break;
				}
			}
			if (bOverlaps)
			{
				continue;
			}

			Candidate.NodeId = OutRooms.Num();
			if (GraphRuleSet->RoomCategories.Num() > 0)
			{
				Candidate.RoomCategory = GraphRuleSet->RoomCategories[
					Stream.RandRange(0, GraphRuleSet->RoomCategories.Num() - 1)];
			}
			OutRooms.Add(Candidate);
			bPlaced = true;
		}
	}

	if (OutRooms.Num() <= 1)
	{
		return; // nothing to connect
	}

	// Minimum-spanning-tree corridors via Prim's, distance = centre-to-centre Manhattan distance.
	TArray<bool> InTree;
	InTree.Init(false, OutRooms.Num());
	InTree[0] = true;
	int32 Connected = 1;

	auto CentreDist = [&](int32 A, int32 B) -> int32
	{
		const FIntPoint CA = OutRooms[A].GetCentreCell();
		const FIntPoint CB = OutRooms[B].GetCentreCell();
		return FMath::Abs(CA.X - CB.X) + FMath::Abs(CA.Y - CB.Y);
	};

	while (Connected < OutRooms.Num())
	{
		int32 BestFrom = INDEX_NONE, BestTo = INDEX_NONE, BestDist = MAX_int32;
		for (int32 A = 0; A < OutRooms.Num(); ++A)
		{
			if (!InTree[A]) { continue; }
			for (int32 B = 0; B < OutRooms.Num(); ++B)
			{
				if (InTree[B]) { continue; }
				const int32 D = CentreDist(A, B);
				if (D < BestDist)
				{
					BestDist = D; BestFrom = A; BestTo = B;
				}
			}
		}
		if (BestTo == INDEX_NONE)
		{
			break;
		}
		OutEdges.Emplace(BestFrom, BestTo, /*bLoop=*/false);
		InTree[BestTo] = true;
		++Connected;
	}

	// Add a data-driven fraction of EXTRA loop edges (random non-tree pairs).
	const int32 TreeEdges = OutEdges.Num();
	const int32 LoopBudget = FMath::Clamp(
		FMath::RoundToInt(TreeEdges * FMath::Clamp(GraphRuleSet->ExtraLoopFraction, 0.f, 1.f)),
		0, TreeEdges);
	for (int32 i = 0; i < LoopBudget; ++i)
	{
		const int32 A = Stream.RandRange(0, OutRooms.Num() - 1);
		int32 B = Stream.RandRange(0, OutRooms.Num() - 1);
		if (A == B)
		{
			B = (B + 1) % OutRooms.Num();
		}
		OutEdges.Emplace(A, B, /*bLoop=*/true);
	}
}

bool ULvl_GraphGeneratorComponent::CollapseTiles(FRandomStream& Stream, const TArray<FLvl_RoomNode>& Rooms,
	const TArray<FLvl_CorridorEdge>& Edges, TArray<FLvl_TileCell>& OutCells) const
{
	OutCells.Reset();
	if (!GraphRuleSet)
	{
		return false;
	}

	const int32 GW = FMath::Max(1, GraphRuleSet->GridDimensions.X);
	const int32 GH = FMath::Max(1, GraphRuleSet->GridDimensions.Y);

	// Kind grid: start Empty, carve rooms, then corridors.
	TArray<ELvl_TileKind> Kind;
	Kind.Init(ELvl_TileKind::Empty, GW * GH);
	TArray<int32> RoomOf;
	RoomOf.Init(INDEX_NONE, GW * GH);

	auto Idx = [&](int32 X, int32 Y) { return Y * GW + X; };
	auto InBounds = [&](int32 X, int32 Y) { return X >= 0 && X < GW && Y >= 0 && Y < GH; };

	// Carve rooms.
	for (const FLvl_RoomNode& Room : Rooms)
	{
		for (int32 Y = Room.OriginCell.Y; Y < Room.OriginCell.Y + Room.SizeCells.Y; ++Y)
		{
			for (int32 X = Room.OriginCell.X; X < Room.OriginCell.X + Room.SizeCells.X; ++X)
			{
				if (InBounds(X, Y))
				{
					Kind[Idx(X, Y)] = ELvl_TileKind::Room;
					RoomOf[Idx(X, Y)] = Room.NodeId;
				}
			}
		}
	}

	// Carve L-shaped corridors between connected room centres.
	const int32 HalfW = FMath::Max(0, GraphRuleSet->CorridorHalfWidthCells);
	auto CarveCell = [&](int32 X, int32 Y)
	{
		for (int32 DY = -HalfW; DY <= HalfW; ++DY)
		{
			for (int32 DX = -HalfW; DX <= HalfW; ++DX)
			{
				const int32 NX = X + DX, NY = Y + DY;
				if (InBounds(NX, NY) && Kind[Idx(NX, NY)] == ELvl_TileKind::Empty)
				{
					Kind[Idx(NX, NY)] = ELvl_TileKind::Corridor;
				}
			}
		}
	};

	for (const FLvl_CorridorEdge& Edge : Edges)
	{
		if (!Rooms.IsValidIndex(Edge.FromNode) || !Rooms.IsValidIndex(Edge.ToNode))
		{
			continue;
		}
		const FIntPoint A = Rooms[Edge.FromNode].GetCentreCell();
		const FIntPoint B = Rooms[Edge.ToNode].GetCentreCell();

		// Deterministic elbow order (horizontal-first vs vertical-first) from the stream.
		const bool bHorizFirst = Stream.FRand() < 0.5f;
		const int32 ElbowX = bHorizFirst ? B.X : A.X;
		const int32 ElbowY = bHorizFirst ? A.Y : B.Y;

		for (int32 X = FMath::Min(A.X, ElbowX); X <= FMath::Max(A.X, ElbowX); ++X) { CarveCell(X, A.Y); }
		for (int32 Y = FMath::Min(A.Y, ElbowY); Y <= FMath::Max(A.Y, ElbowY); ++Y) { CarveCell(ElbowX, Y); }
		for (int32 X = FMath::Min(ElbowX, B.X); X <= FMath::Max(ElbowX, B.X); ++X) { CarveCell(X, B.Y); }
		for (int32 Y = FMath::Min(ElbowY, B.Y); Y <= FMath::Max(ElbowY, B.Y); ++Y) { CarveCell(B.X, Y); }
	}

	// Promote room-edge cells adjacent to a corridor into Door cells (good for door prefabs).
	for (int32 Y = 0; Y < GH; ++Y)
	{
		for (int32 X = 0; X < GW; ++X)
		{
			if (Kind[Idx(X, Y)] != ELvl_TileKind::Room)
			{
				continue;
			}
			for (int32 Dir = 0; Dir < 4; ++Dir)
			{
				const int32 NX = X + GCardinals[Dir].X;
				const int32 NY = Y + GCardinals[Dir].Y;
				if (InBounds(NX, NY) && Kind[Idx(NX, NY)] == ELvl_TileKind::Corridor)
				{
					Kind[Idx(X, Y)] = ELvl_TileKind::Door;
					break;
				}
			}
		}
	}

	// WFC-style collapse: for each carved cell, pick a tile rule whose connector mask opens toward each
	// carved neighbour. When TileRules is empty the cell records its kind with no tile tag (inert carve).
	const bool bHaveTileRules = GraphRuleSet->TileRules.Num() > 0;
	int32 Iterations = 0;
	const int32 MaxIter = FMath::Max(1, GraphRuleSet->MaxCollapseIterations);

	for (int32 Y = 0; Y < GH; ++Y)
	{
		for (int32 X = 0; X < GW; ++X)
		{
			const ELvl_TileKind K = Kind[Idx(X, Y)];
			if (K == ELvl_TileKind::Empty)
			{
				continue;
			}

			FLvl_TileCell Cell(FIntPoint(X, Y), K);
			Cell.OwningRoom = RoomOf[Idx(X, Y)];

			if (bHaveTileRules && Iterations < MaxIter)
			{
				++Iterations;

				// Required-open mask: a bit per carved neighbour direction.
				uint8 RequiredOpen = 0;
				for (int32 Dir = 0; Dir < 4; ++Dir)
				{
					const int32 NX = X + GCardinals[Dir].X;
					const int32 NY = Y + GCardinals[Dir].Y;
					if (InBounds(NX, NY) && Kind[Idx(NX, NY)] != ELvl_TileKind::Empty)
					{
						RequiredOpen |= static_cast<uint8>(ConnectorForDirection(Dir));
					}
				}

				// Pick a weighted tile rule that (a) allows this kind and (b) exposes all required opens.
				float TotalWeight = 0.f;
				TArray<const FLvl_WfcTileRule*, TInlineAllocator<16>> Candidates;
				for (const FLvl_WfcTileRule& Rule : GraphRuleSet->TileRules)
				{
					if (!Rule.AllowsKind(K))
					{
						continue;
					}
					if ((Rule.ConnectorMask & RequiredOpen) != RequiredOpen)
					{
						continue; // does not open toward every carved neighbour
					}
					Candidates.Add(&Rule);
					TotalWeight += FMath::Max(0.f, Rule.Weight);
				}

				const FLvl_WfcTileRule* Chosen = nullptr;
				if (Candidates.Num() > 0)
				{
					if (TotalWeight <= 0.f)
					{
						Chosen = Candidates[Stream.RandRange(0, Candidates.Num() - 1)];
					}
					else
					{
						float Roll = Stream.FRandRange(0.f, TotalWeight);
						for (const FLvl_WfcTileRule* Rule : Candidates)
						{
							Roll -= FMath::Max(0.f, Rule->Weight);
							if (Roll <= 0.f) { Chosen = Rule; break; }
						}
						if (!Chosen) { Chosen = Candidates.Last(); }
					}
				}
				if (Chosen)
				{
					Cell.TileTag = Chosen->TileTag;
				}
			}

			OutCells.Add(Cell);
		}
	}

	return OutCells.Num() > 0;
}

void ULvl_GraphGeneratorComponent::StampPrefabs(const TArray<FLvl_TileCell>& Cells, FRandomStream& Stream,
	FLvl_PlacementManifest& OutManifest) const
{
	if (!GraphRuleSet)
	{
		return;
	}

	const FTransform OwnerXform = GetOwner() ? GetOwner()->GetActorTransform() : FTransform::Identity;
	const int32 MaxStamps = FMath::Max(0, GraphRuleSet->MaxStamps);
	const int32 Seed = OutManifest.RandomSeed;

	int32 Stamped = 0;
	int32 CellIndex = 0;
	for (const FLvl_TileCell& Cell : Cells)
	{
		++CellIndex;
		if (Stamped >= MaxStamps)
		{
			break;
		}

		for (const FLvl_PrefabStampRule& Rule : GraphRuleSet->PrefabStamps)
		{
			if (!Rule.IsUsable() || !Rule.Matches(Cell.Kind, Cell.TileTag))
			{
				continue;
			}
			// Deterministic stamp roll.
			if (Stream.FRand() > FMath::Clamp(Rule.StampChance, 0.f, 1.f))
			{
				continue;
			}

			FVector Local = GraphRuleSet->CellToLocal(Cell.Cell);
			Local.Z += Rule.VerticalOffset;
			FVector World = OwnerXform.TransformPosition(Local);

			FQuat Rot = FQuat::Identity;
			if (Rule.bFaceInteriorCentre)
			{
				const FVector Centre = OwnerXform.TransformPosition(GraphRuleSet->CellToLocal(
					FIntPoint(GraphRuleSet->GridDimensions.X / 2, GraphRuleSet->GridDimensions.Y / 2)));
				const FVector Dir = (Centre - World).GetSafeNormal2D();
				if (!Dir.IsNearlyZero())
				{
					Rot = Dir.ToOrientationQuat();
				}
			}

			const FTransform Xform(Rot, World, FVector::OneVector);
			// Deterministic id from seed + a stable (cell index, rule) mix.
			const uint32 A = static_cast<uint32>(Seed);
			const uint32 B = static_cast<uint32>(CellIndex) ^ GetTypeHash(Rule.ActorClassTag);
			const FGuid Id(HashCombine(A, 0x9E3779B9u), HashCombine(A ^ 0x85EBCA6Bu, B),
				HashCombine(B, 0xC2B2AE35u), HashCombine(A * 2654435761u, B * 40503u));

			OutManifest.Entries.Emplace(Rule.ActorClassTag, Xform, Id);
			++Stamped;
			break; // one stamp per cell
		}
	}
}

bool ULvl_GraphGeneratorComponent::GenerateGraph()
{
	// AUTHORITY GUARD AT THE TOP — clients never generate.
	if (!HasWorldAuthority())
	{
		return false;
	}
	if (!GraphRuleSet)
	{
		UE_LOG(LogDP, Warning, TEXT("Lvl GraphGen (%s): GenerateGraph with no GraphRuleSet."), *GetNameSafe(GetOwner()));
		return false;
	}

	ULvl_ProceduralPlacerComponent* Placer = ResolveTargetPlacer();
	if (!Placer)
	{
		UE_LOG(LogDP, Warning, TEXT("Lvl GraphGen (%s): no target ULvl_ProceduralPlacerComponent."),
			*GetNameSafe(GetOwner()));
		return false;
	}

	const int32 Seed = GetEffectiveSeed();
	FRandomStream Stream(Seed);

	TArray<FLvl_RoomNode> Rooms;
	TArray<FLvl_CorridorEdge> Edges;
	BuildRoomGraph(Stream, Rooms, Edges);

	TArray<FLvl_TileCell> Cells;
	if (!CollapseTiles(Stream, Rooms, Edges, Cells))
	{
		UE_LOG(LogDP, Verbose, TEXT("Lvl GraphGen (%s): produced no carved cells."), *GetNameSafe(GetOwner()));
		return false;
	}

	GraphManifest.Reset();
	GraphManifest.RuleSetTag = GraphRuleSet->DataTag;
	GraphManifest.RegionTag = GetEffectiveRegionTag();
	GraphManifest.RandomSeed = Seed;
	StampPrefabs(Cells, Stream, GraphManifest);

	const int32 Spawned = Placer->RestoreFromManifest(GraphManifest);
	BroadcastGraphGenerated(Spawned);

	UE_LOG(LogDP, Log, TEXT("Lvl GraphGen (%s): %d rooms, %d edges, %d cells, %d entries, %d spawned (seed %d)."),
		*GetNameSafe(GetOwner()), Rooms.Num(), Edges.Num(), Cells.Num(), GraphManifest.Num(), Spawned, Seed);
	return Spawned > 0;
}

void ULvl_GraphGeneratorComponent::BroadcastGraphGenerated(int32 PlacedCount) const
{
	UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		return;
	}
	FLvl_PlacementEventPayload Payload;
	Payload.RuleSetTag = GraphManifest.RuleSetTag;
	Payload.RegionTag = GraphManifest.RegionTag;
	Payload.RandomSeed = GraphManifest.RandomSeed;
	Payload.PlacedCount = PlacedCount;
	Bus->BroadcastPayload(LvlNativeTags::Bus_Lvl_Placement_Generated, FInstancedStruct::Make(Payload),
		const_cast<ULvl_GraphGeneratorComponent*>(this));
}

//~ ISeam_Persistable ------------------------------------------------------------------------------

void ULvl_GraphGeneratorComponent::CaptureState_Implementation(FInstancedStruct& Out) const
{
	FLvl_GraphSaveRecord Record;
	Record.GraphRuleSetTag = GraphRuleSet ? GraphRuleSet->DataTag : FGameplayTag();
	Record.RegionTag = GetEffectiveRegionTag();
	Record.RandomSeed = GraphManifest.RandomSeed != 0 ? GraphManifest.RandomSeed : GetEffectiveSeed();
	Record.Manifest = GraphManifest;
	Record.RestoreStrategy = RestoreStrategy;
	Out = FInstancedStruct::Make(Record);
}

void ULvl_GraphGeneratorComponent::RestoreState_Implementation(const FInstancedStruct& In)
{
	// AUTHORITY GUARD AT THE TOP — a client-side load is a no-op (clients get the actors via replication).
	if (!HasWorldAuthority())
	{
		return;
	}
	const FLvl_GraphSaveRecord* Record = In.GetPtr<FLvl_GraphSaveRecord>();
	if (!Record)
	{
		UE_LOG(LogDP, Warning, TEXT("Lvl GraphGen (%s): RestoreState with a non-graph record."), *GetNameSafe(GetOwner()));
		return;
	}
	if (!FLvl_SaveGameRegenHelpers::IsRecordUsable(*Record))
	{
		return; // helper logged the reason
	}

	ULvl_ProceduralPlacerComponent* Placer = ResolveTargetPlacer();
	if (!Placer)
	{
		return;
	}

	// RegenerateFromSeed: re-run the generator from the stored seed (self-healing if content changed);
	// fall back to verbatim if regeneration produces nothing. RestoreManifestVerbatim: replay exactly.
	bool bRegenerated = false;
	if (FLvl_SaveGameRegenHelpers::ShouldRegenerate(*Record) && GraphRuleSet)
	{
		const int32 PrevSeedOverride = SeedOverride;
		SeedOverride = Record->RandomSeed; // adopt the stored seed for an exact reproduction
		bRegenerated = GenerateGraph();
		SeedOverride = PrevSeedOverride;
	}

	if (!bRegenerated)
	{
		// Verbatim replay (also the documented fallback for a failed regeneration).
		GraphManifest = Record->Manifest;
		const int32 Spawned = Placer->RestoreFromManifest(GraphManifest);
		UE_LOG(LogDP, Log, TEXT("Lvl GraphGen (%s): restored %d entries verbatim (seed %d)."),
			*GetNameSafe(GetOwner()), Spawned, GraphManifest.RandomSeed);
	}
}

FGameplayTag ULvl_GraphGeneratorComponent::GetPersistenceKind_Implementation() const
{
	return LvlNativeTags::Persist_Lvl_Graph;
}
