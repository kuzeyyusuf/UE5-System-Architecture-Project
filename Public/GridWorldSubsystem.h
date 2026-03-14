// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GridWorldSubsystem.generated.h"

/*
 * Cell state enum
 */
UENUM(BlueprintType)
enum class ECellState : uint8
{
	Empty     UMETA(DisplayName = "Empty"),
	Reserved  UMETA(DisplayName = "Reserved"),
	Committed UMETA(DisplayName = "Committed")
};

/*
 * Per-cell occupancy data (save-safe)
 */
USTRUCT(BlueprintType)
struct FCellOccupancy
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	ECellState State = ECellState::Empty;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	int32 ReservationId = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	int32 BuildingId = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	int32 OwnerId = -1;
};

USTRUCT(BlueprintType)
struct FReservationRecord
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	int32 ReservationId = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	int32 OwnerId = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	TArray<FIntPoint> Cells;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	float ExpiresAt = 0.0f;

};

USTRUCT(BlueprintType)
struct FBuildingPlacementRecord
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save")
	int32 BuildingId = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save")
	int32 OwnerId = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save")
	FIntPoint OriginCell = FIntPoint(0, 0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save")
	FIntPoint Size = FIntPoint(1, 1);
};



/*
 * World Subsystem: Grid Occupancy Core
 */
UCLASS(BlueprintType)
class ABOBA_API UGridWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/* Initialize grid (one-time) */
	UFUNCTION(BlueprintCallable, Category = "Grid|Core")
	void InitGrid(int32 InWidth, int32 InHeight);

	/* Reset grid to empty */
	UFUNCTION(BlueprintCallable, Category = "Grid|Core")
	void ClearGrid();

	/* Cell <-> Index helpers */
	UFUNCTION(BlueprintPure, Category = "Grid|Core")
	bool CellToIndex(FIntPoint Cell, int32& OutIndex) const;

	UFUNCTION(BlueprintPure, Category = "Grid|Core")
	bool IndexToCell(int32 Index, FIntPoint& OutCell) const;

	/* Read / Write cell */
	UFUNCTION(BlueprintCallable, Category = "Grid|Core")
	bool GetCellData(FIntPoint Cell, FCellOccupancy& OutData) const;

	UFUNCTION(BlueprintCallable, Category = "Grid|Core")
	bool SetCellData(FIntPoint Cell, const FCellOccupancy& NewData);

	UFUNCTION(BlueprintCallable, Category = "Grid|Save")
	bool ApplyCommittedArea(FIntPoint OriginCell, FIntPoint Size, int32 BuildingInstanceId, int32 OwnerId);

	/* Info */
	UFUNCTION(BlueprintPure, Category = "Grid|Core")
	bool IsGridInitialized() const { return bGridInitialized; }

	UFUNCTION(BlueprintPure, Category = "Grid|Core")
	int32 GetGridWidth() const { return GridWidth; }

	UFUNCTION(BlueprintPure, Category = "Grid|Core")
	int32 GetGridHeight() const { return GridHeight; }

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;


	// --- Adým 4 Core API ---

// Bir alan tamamen boþ mu? (Empty kontrol)
	UFUNCTION(BlueprintCallable, Category = "Grid|Reserve")
	bool IsAreaFree(FIntPoint OriginCell, FIntPoint Size) const;

	// Reserve: alaný kilitle, ReservationId döndür (baþarýlýysa >=0)
	UFUNCTION(BlueprintCallable, Category = "Grid|Reserve")
	bool ReserveArea(FIntPoint OriginCell, FIntPoint Size, int32 OwnerId, int32& OutReservationId);

	// Commit: reservation'ý binaya çevir
	UFUNCTION(BlueprintCallable, Category = "Grid|Reserve")
	bool CommitReservation(int32 ReservationId, int32 BuildingId);

	// Release: reservation iptal
	UFUNCTION(BlueprintCallable, Category = "Grid|Reserve")
	bool ReleaseReservation(int32 ReservationId);

	UFUNCTION(BlueprintCallable, Category = "Grid|Reserve")
	bool RefreshReservation(int32 ReservationId);

	UFUNCTION(BlueprintCallable, Category = "Grid|Debug")
	bool ValidateCell(FIntPoint Cell, FString& OutError) const;

	UFUNCTION(BlueprintCallable, Category = "Grid|Save")
	void ExportCommittedBuildings(TArray<FBuildingPlacementRecord>& OutBuildings) const;

	UFUNCTION(BlueprintCallable, Category = "Grid|Save")
	void ApplyCommittedBuildings(const TArray<FBuildingPlacementRecord>& Buildings);

	UFUNCTION(BlueprintCallable, Category = "Grid|Demolish")
	bool ClearCommittedArea(FIntPoint OriginCell, FIntPoint Size, int32 BuildingInstanceId);


private:
	UPROPERTY(EditAnywhere, Category = "Grid|Config")
	int32 GridWidth = 256;

	UPROPERTY(EditAnywhere, Category = "Grid|Config")
	int32 GridHeight = 256;

	UPROPERTY()
	TArray<FCellOccupancy> Cells;

	UPROPERTY()
	bool bGridInitialized = false;

	// Active reservations
	UPROPERTY()
	TMap<int32, FReservationRecord> Reservations;

	// Incrementing id generator
	UPROPERTY()
	int32 NextReservationId = 1; // 0 ve -1'den kaçýnmak için 1'den baþla

	// Timeout config (seconds). 0 = disabled
	UPROPERTY(EditAnywhere, Category = "Grid|Reserve")
	float ReservationTTLSeconds = 3.0f;

	// Cleanup tick period
	UPROPERTY(EditAnywhere, Category = "Grid|Reserve")
	float ReservationCleanupInterval = 0.5f;

	FTimerHandle ReservationCleanupTimer;

	void CleanupExpiredReservations();


	int32 GetTotalCellCount() const
	{
		return GridWidth * GridHeight;
	}

	bool EnumerateAreaCells(FIntPoint OriginCell, FIntPoint Size, TArray<FIntPoint>& OutCells) const;

};

