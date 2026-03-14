// Fill out your copyright notice in the Description page of Project Settings.

#include "GridWorldSubsystem.h"
#include "Engine/World.h"
#include "TimerManager.h"

void UGridWorldSubsystem::InitGrid(int32 InWidth, int32 InHeight)
{
	if (bGridInitialized)
	{
		return;
	}

	if (InWidth > 0)
	{
		GridWidth = InWidth;
	}

	if (InHeight > 0)
	{
		GridHeight = InHeight;
	}

	const int32 TotalCells = GetTotalCellCount();
	Cells.SetNum(TotalCells);

	// Default struct values already reset cells to Empty
	bGridInitialized = true;
}

void UGridWorldSubsystem::ClearGrid()
{
	if (!bGridInitialized)
	{
		return;
	}

	const int32 TotalCells = GetTotalCellCount();
	Cells.SetNum(TotalCells);

	for (int32 i = 0; i < TotalCells; ++i)
	{
		Cells[i] = FCellOccupancy{};
	}
}

bool UGridWorldSubsystem::CellToIndex(FIntPoint Cell, int32& OutIndex) const
{
	if (!bGridInitialized)
	{
		OutIndex = -1;
		return false;
	}

	const bool bInBounds =
		Cell.X >= 0 && Cell.Y >= 0 &&
		Cell.X < GridWidth && Cell.Y < GridHeight;

	if (!bInBounds)
	{
		OutIndex = -1;
		return false;
	}

	OutIndex = Cell.X + Cell.Y * GridWidth;
	return true;
}

bool UGridWorldSubsystem::IndexToCell(int32 Index, FIntPoint& OutCell) const
{
	if (!bGridInitialized)
	{
		OutCell = FIntPoint(0, 0);
		return false;
	}

	const int32 TotalCells = GetTotalCellCount();
	if (Index < 0 || Index >= TotalCells)
	{
		OutCell = FIntPoint(0, 0);
		return false;
	}

	const int32 X = Index % GridWidth;
	const int32 Y = Index / GridWidth;

	OutCell = FIntPoint(X, Y);
	return true;
}

bool UGridWorldSubsystem::GetCellData(FIntPoint Cell, FCellOccupancy& OutData) const
{
	if (!bGridInitialized)
	{
		return false;
	}

	int32 Index;
	if (!CellToIndex(Cell, Index))
	{
		return false;
	}

	OutData = Cells[Index];
	return true;
}

bool UGridWorldSubsystem::SetCellData(FIntPoint Cell, const FCellOccupancy& NewData)
{
	if (!bGridInitialized)
	{
		return false;
	}

	int32 Index;
	if (!CellToIndex(Cell, Index))
	{
		return false;
	}

	Cells[Index] = NewData;
	return true;
}

bool UGridWorldSubsystem::EnumerateAreaCells(FIntPoint OriginCell, FIntPoint Size, TArray<FIntPoint>& OutCells) const
{
	OutCells.Reset();

	if (!bGridInitialized) return false;
	if (Size.X <= 0 || Size.Y <= 0) return false;

	// Bounds: tüm footprint world içinde mi?
	// Origin dahil, Size kadar saða/aþaðý (X,Y) yayýlacaðýz:
	// Cell = (Origin.X + dx, Origin.Y + dy), dx:[0..Size.X-1], dy:[0..Size.Y-1]
	const int32 MaxX = OriginCell.X + Size.X - 1;
	const int32 MaxY = OriginCell.Y + Size.Y - 1;

	if (OriginCell.X < 0 || OriginCell.Y < 0) return false;
	if (MaxX >= GridWidth || MaxY >= GridHeight) return false;

	OutCells.Reserve(Size.X * Size.Y);

	for (int32 y = 0; y < Size.Y; ++y)
	{
		for (int32 x = 0; x < Size.X; ++x)
		{
			OutCells.Add(FIntPoint(OriginCell.X + x, OriginCell.Y + y));
		}
	}

	return true;
}

bool UGridWorldSubsystem::IsAreaFree(FIntPoint OriginCell, FIntPoint Size) const
{
	if (!bGridInitialized) return false;

	TArray<FIntPoint> AreaCells;
	if (!EnumerateAreaCells(OriginCell, Size, AreaCells))
	{
		return false;
	}

	for (const FIntPoint& Cell : AreaCells)
	{
		int32 Index;
		if (!CellToIndex(Cell, Index))
		{
			return false;
		}

		// Sadece Empty kabul ediyoruz
		if (Cells[Index].State != ECellState::Empty)
		{
			return false;
		}
	}

	return true;
}

bool UGridWorldSubsystem::ReserveArea(FIntPoint OriginCell, FIntPoint Size, int32 OwnerId, int32& OutReservationId)
{
	OutReservationId = -1;

	if (!bGridInitialized) return false;
	if (OwnerId < 0) return false;

	TArray<FIntPoint> AreaCells;
	if (!EnumerateAreaCells(OriginCell, Size, AreaCells))
	{
		return false;
	}

	// 1) Önce kontrol: hepsi Empty mi?
	for (const FIntPoint& Cell : AreaCells)
	{
		int32 Index;
		if (!CellToIndex(Cell, Index)) return false;
		if (Cells[Index].State != ECellState::Empty) return false;
	}

	// 2) ReservationId üret
	const int32 ReservationId = NextReservationId++;
	OutReservationId = ReservationId;

	// 3) Kayýt oluþtur
	FReservationRecord Record;
	Record.ReservationId = ReservationId;
	Record.OwnerId = OwnerId;
	Record.Cells = AreaCells;

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	Record.ExpiresAt = (ReservationTTLSeconds > 0.0f) ? (Now + ReservationTTLSeconds) : 0.0f;


	Reservations.Add(ReservationId, Record);

	// 4) Hücreleri Reserved yap
	for (const FIntPoint& Cell : AreaCells)
	{
		int32 Index;
		CellToIndex(Cell, Index);

		FCellOccupancy& C = Cells[Index];
		C.State = ECellState::Reserved;
		C.ReservationId = ReservationId;
		C.OwnerId = OwnerId;
		C.BuildingId = -1;
	}

	return true;
}

bool UGridWorldSubsystem::CommitReservation(int32 ReservationId, int32 BuildingId)
{
	if (!bGridInitialized) return false;
	if (ReservationId <= 0) return false;
	if (BuildingId < 0) return false;

	FReservationRecord* Record = Reservations.Find(ReservationId);
	if (!Record) return false;

	// 1) Invariant: tüm hücreler Reserved ve ReservationId eþleþiyor mu?
	for (const FIntPoint& Cell : Record->Cells)
	{
		int32 Index;
		if (!CellToIndex(Cell, Index)) return false;

		const FCellOccupancy& C = Cells[Index];
		if (C.State != ECellState::Reserved) return false;
		if (C.ReservationId != ReservationId) return false;
	}

	// 2) Commit et
	for (const FIntPoint& Cell : Record->Cells)
	{
		int32 Index;
		CellToIndex(Cell, Index);

		FCellOccupancy& C = Cells[Index];
		C.State = ECellState::Committed;
		C.BuildingId = BuildingId;

		// Reservation bitti:
		C.ReservationId = -1;

		
	}

	// 3) Reservation kaydýný sil
	Reservations.Remove(ReservationId);
	return true;
}

bool UGridWorldSubsystem::ReleaseReservation(int32 ReservationId)
{
	if (!bGridInitialized) return false;
	if (ReservationId <= 0) return false;

	FReservationRecord* Record = Reservations.Find(ReservationId);
	if (!Record) return false;

	// 1) Reserved hücreleri Empty yap
	for (const FIntPoint& Cell : Record->Cells)
	{
		int32 Index;
		if (!CellToIndex(Cell, Index)) continue;

		FCellOccupancy& C = Cells[Index];

		// Sadece o reservation'a ait Reserved ise temizle
		if (C.State == ECellState::Reserved && C.ReservationId == ReservationId)
		{
			C = FCellOccupancy{}; // default: Empty + -1
		}
	}

	// 2) Kaydý sil
	Reservations.Remove(ReservationId);
	return true;
}

void UGridWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Timer kur: periyodik cleanup
	if (ReservationCleanupInterval > 0.0f)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				ReservationCleanupTimer,
				this,
				&UGridWorldSubsystem::CleanupExpiredReservations,
				ReservationCleanupInterval,
				true
			);
		}
	}
}

void UGridWorldSubsystem::Deinitialize()
{
	// Timer temizle
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReservationCleanupTimer);
	}

	Super::Deinitialize();
}

void UGridWorldSubsystem::CleanupExpiredReservations()
{
	if (!bGridInitialized) return;
	if (ReservationTTLSeconds <= 0.0f) return;

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;

	TArray<int32> ToRelease;
	ToRelease.Reserve(Reservations.Num());

	for (const auto& Pair : Reservations)
	{
		const int32 ResId = Pair.Key;
		const FReservationRecord& Rec = Pair.Value;

		// ExpiresAt 0 ise: güvenli tarafta kal, dokunma 
		if (Rec.ExpiresAt > 0.0f && Now >= Rec.ExpiresAt)
		{
			ToRelease.Add(ResId);
		}
	}

	for (int32 ResId : ToRelease)
	{
		ReleaseReservation(ResId);
	}
}

bool UGridWorldSubsystem::RefreshReservation(int32 ReservationId)
{
	if (!bGridInitialized) return false;

	FReservationRecord* Record = Reservations.Find(ReservationId);
	if (!Record) return false;

	if (ReservationTTLSeconds <= 0.0f)
	{
		Record->ExpiresAt = 0.0f;
		return true;
	}

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	Record->ExpiresAt = Now + ReservationTTLSeconds;
	return true;
}

bool UGridWorldSubsystem::ValidateCell(FIntPoint Cell, FString& OutError) const
{
	OutError = "";

	if (!bGridInitialized)
	{
		OutError = "Grid not initialized";
		return false;
	}

	int32 Index;
	if (!CellToIndex(Cell, Index))
	{
		OutError = "Out of bounds";
		return false;
	}

	const FCellOccupancy& C = Cells[Index];

	switch (C.State)
	{
	case ECellState::Empty:
		if (C.ReservationId != -1 || C.BuildingId != -1 || C.OwnerId != -1)
		{
			OutError = "Empty cell must have -1 ids";
			return false;
		}
		break;

	case ECellState::Reserved:
		if (C.ReservationId <= 0)
		{
			OutError = "Reserved cell must have valid ReservationId";
			return false;
		}
		if (C.BuildingId != -1)
		{
			OutError = "Reserved cell must not have BuildingId";
			return false;
		}
		break;

	case ECellState::Committed:
		if (C.BuildingId < 0)
		{
			OutError = "Committed cell must have BuildingId";
			return false;
		}
		if (C.ReservationId != -1)
		{
			OutError = "Committed cell must not have ReservationId";
			return false;
		}
		break;
	}

	return true;
}


void UGridWorldSubsystem::ExportCommittedBuildings(TArray<FBuildingPlacementRecord>& OutBuildings) const
{
	OutBuildings.Reset();

	if (!bGridInitialized) return;

	

	TMap<int32, FBuildingPlacementRecord> ByBuilding;

	for (int32 Index = 0; Index < Cells.Num(); ++Index)
	{
		const FCellOccupancy& C = Cells[Index];
		if (C.State != ECellState::Committed) continue;

		// Cell koordinatý
		FIntPoint Cell;
		IndexToCell(Index, Cell);

		// Ayný BuildingId için ilk gördüðümüz cell'i origin kabul edilir
		if (!ByBuilding.Contains(C.BuildingId))
		{
			FBuildingPlacementRecord R;
			R.BuildingId = C.BuildingId;
			R.OwnerId = C.OwnerId;
			R.OriginCell = Cell;
			R.Size = FIntPoint(1, 1); 
			ByBuilding.Add(C.BuildingId, R);
		}
	}

	ByBuilding.GenerateValueArray(OutBuildings);
}

void UGridWorldSubsystem::ApplyCommittedBuildings(const TArray<FBuildingPlacementRecord>& Buildings)
{
	if (!bGridInitialized) return;

	// Runtime rezervasyonlarý temizle
	Reservations.Empty();

	// Grid'i boþalt
	ClearGrid();

	// Buildings'ý committed olarak bas
	for (const FBuildingPlacementRecord& B : Buildings)
	{
		if (B.BuildingId < 0) continue;

		// Footprint þu an 1x1 default;
		FIntPoint Size = B.Size;
		if (Size.X <= 0) Size.X = 1;
		if (Size.Y <= 0) Size.Y = 1;

		TArray<FIntPoint> AreaCells;
		if (!EnumerateAreaCells(B.OriginCell, Size, AreaCells))
		{
			continue;
		}

		for (const FIntPoint& Cell : AreaCells)
		{
			int32 Index;
			if (!CellToIndex(Cell, Index)) continue;

			FCellOccupancy& C = Cells[Index];
			C.State = ECellState::Committed;
			C.BuildingId = B.BuildingId;
			C.OwnerId = B.OwnerId;
			C.ReservationId = -1;
		}
	}
}

bool UGridWorldSubsystem::ApplyCommittedArea(FIntPoint OriginCell, FIntPoint Size, int32 BuildingInstanceId, int32 OwnerId)
{
	if (!bGridInitialized) return false;
	if (BuildingInstanceId < 0) return false;

	TArray<FIntPoint> AreaCells;
	if (!EnumerateAreaCells(OriginCell, Size, AreaCells)) return false;

	// Çakýþma kontrolü (istersen kapatýrýz)
	for (const FIntPoint& Cell : AreaCells)
	{
		int32 Index;
		if (!CellToIndex(Cell, Index)) return false;
		if (Cells[Index].State != ECellState::Empty) return false;
	}

	for (const FIntPoint& Cell : AreaCells)
	{
		int32 Index;
		CellToIndex(Cell, Index);

		FCellOccupancy& C = Cells[Index];
		C.State = ECellState::Committed;
		C.BuildingId = BuildingInstanceId;
		C.OwnerId = OwnerId;
		C.ReservationId = -1;
	}

	return true;
}

bool UGridWorldSubsystem::ClearCommittedArea(FIntPoint OriginCell, FIntPoint Size, int32 BuildingInstanceId)
{
	if (!bGridInitialized) return false;
	if (BuildingInstanceId < 0) return false;

	TArray<FIntPoint> AreaCells;
	if (!EnumerateAreaCells(OriginCell, Size, AreaCells)) return false;

	// Önce doðrula: bu alan gerçekten ayný instance'a mý ait?
	for (const FIntPoint& Cell : AreaCells)
	{
		int32 Index;
		if (!CellToIndex(Cell, Index)) return false;

		const FCellOccupancy& C = Cells[Index];

		if (C.State != ECellState::Committed) return false;
		if (C.BuildingId != BuildingInstanceId) return false;
	}

	// Sonra temizle
	for (const FIntPoint& Cell : AreaCells)
	{
		int32 Index;
		CellToIndex(Cell, Index);

		Cells[Index] = FCellOccupancy{};
	}

	return true;
}




