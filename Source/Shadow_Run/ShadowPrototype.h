#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "ShadowPrototype.generated.h"

class UCameraComponent;
class UPointLightComponent;
class UStaticMeshComponent;
class USoundBase;
class UTexture2D;
class UFont;
class UShadowProfile;

enum class EShadowMenu : uint8 { None, Main, Pause, Settings, Help, Build, ConfirmMain };
enum class EShadowMenuAction : uint8 { Play, Settings, Help, Quit, Resume, Build, Main, SensitivityDown,
    SensitivityUp, VolumeDown, VolumeUp, Back, LeaveRun };

UCLASS()
class AShadowPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual bool InputKey(const FInputKeyEventArgs& Params) override;
    void SetMenuInput(bool bEnabled);

private:
    TOptional<bool> bMenuInput;
};

struct FShadowShopCard
{
    FString Title;
    FString Detail;
    int32 Price = 0;
    bool bAvailable = false;
};

UCLASS()
class AShadowRunner : public ACharacter
{
    GENERATED_BODY()
public:
    AShadowRunner();
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    void HitByDrone(int32 Damage);
    int32 Health = 100;
    int32 MaxHealth = 100;
    int32 Shield = 0;
    int32 MaxShield = 0;
    float DamageMultiplier = 1.f;
    float ReloadMultiplier = 1.f;
    int32 Magazine = 24;
    int32 Reserve = 96;
    bool IsReloading() const { return ReloadTimer.IsValid(); }
    float GetReloadProgress() const;
    float GetReloadDuration() const;
    bool HasPistolEquipped() const { return bPistolEquipped; }
    bool HasCarbine() const { return bCarbineCollected; }
    void CollectCarbine();
    void ReleaseCombatInput();

private:
    void Forward(float Value);
    void Strafe(float Value);
    void LookYaw(float Value);
    void LookPitch(float Value);
    void StartFire();
    void StopFire();
    void Fire();
    void StartAim();
    void StopAim();
    void Reload();
    void FinishReload();
    void Interact();
    void Pause();
    void Restart();
    void EquipCarbine();
    void EquipPistol();
    void ShopThree();
    void ShopFour();
    void ConfirmShop();
    bool SelectShop(int32 Index);
    void EquipWeapon(bool bPistol);
    void UpdateWeaponPose();

    UPROPERTY()
    TObjectPtr<UCameraComponent> Eyes;
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> Rifle;
    UPROPERTY()
    TObjectPtr<UPointLightComponent> MuzzleFlash;
    FVector RifleCenter = FVector::ZeroVector;
    FVector MuzzleLocal = FVector::ZeroVector;
    double RifleScale = 1;
    float WeaponYaw = 180;
    int32 CarbineMagazine = 24;
    int32 CarbineReserve = 96;
    int32 PistolMagazine = 12;
    int32 PistolReserve = 48;
    bool bPistolEquipped = false;
    bool bCarbineCollected = false;
    float AimBlend = 0;
    float Recoil = 0;
    float FlashRemaining = 0;
    bool bFireHeld = false;
    bool bAimHeld = false;
    bool bDryFirePlayed = false;
    FTimerHandle ReloadTimer;
    double NextShot = 0;
};

UCLASS()
class AShadowPrototype : public AGameModeBase
{
    GENERATED_BODY()
public:
    AShadowPrototype();
    virtual void StartPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    void Fire(AShadowRunner& Player, const FVector& Origin, const FVector& Direction, const FVector& Muzzle);
    void Interact(AShadowRunner& Player);
    void Finish(bool bEscaped);
    void Sound(const TCHAR* Name, float Gain = .65f) const;
    void ShotEffects(const FVector& Start, const FVector& End, bool bDrone, bool bImpact, bool bBarrier = false);
    bool TryShopChoice(AShadowRunner& Player, int32 Index);
    void CloseShop();
    int32 GetShopSelection() const { return ShopSelection; }
    bool SelectShopCard(int32 Index);
    void MoveShopSelection(int32 X, int32 Y);
    bool IsShopOpen() const { return bShopOpen; }
    bool HasBoughtPerk() const { return bPerkBoughtThisVisit; }
    bool HasWorkbench() const { return IsWorkbenchRoom(); }
    FVector GetWorkbenchPosition() const;
    TArray<FShadowShopCard> GetShopOptions(const AShadowRunner& Player) const;
    FString GetBuildSummary(const AShadowRunner& Player) const;
    EShadowMenu GetMenu() const { return Menu; }
    TArray<EShadowMenuAction> GetMenuActions() const;
    void ToggleMenu();
    void ActivateMenuAction(EShadowMenuAction Action);
    void MoveMenuSelection(int32 Direction);
    void ConfirmMenu();
    int32 GetMenuSelection() const { return MenuSelection; }
    int32 GetSensitivity() const { return Sensitivity; }
    int32 GetVolume() const { return Volume; }
    int32 GetCredits() const;
    int32 GetBestScore() const;
    int32 Score = 0;
    int32 RunCreditsEarned = 0;
    int32 RunCreditsSpent = 0;

    int32 Carried = 0;
    int32 Deposited = 0;
    int32 Kills = 0;
    bool bFinished = false;
    bool bWon = false;
    FString Feedback;
    double FeedbackUntil = 0;
    double HitMarkerUntil = 0;
    FVector GetObjectivePosition(const FVector& PlayerLocation) const;
    int32 GetCellsNeeded() const { return Current.Kind == ERoomKind::Workshop ? 0 : 1; }
    bool IsWorkshop() const { return Current.Kind == ERoomKind::Workshop; }
    bool IsPassageOpen() const { return bNextReady && GateHeight >= 220.f; }
    int32 GetPerks(int32 Track) const;
    int32 FloorNumber = 1;
    int32 RoomsCleared = 0;
    FVector TerminalPosition = FVector(2990, 270, 64);

private:
    struct FPickup
    {
        AActor* Actor = nullptr;
        bool bCell = false;
    };

    struct FDrone
    {
        AActor* Actor = nullptr;
        FVector PatrolOrigin = FVector::ZeroVector;
        float DamageTaken = 0.f;
        bool bRewarded = false;
        double NextAttack = 0;
        double RespawnAt = 0;
    };

    enum class ERoomKind : uint8 { Reception, Cargo, Machines, Workshop };
    struct FRoom
    {
        ERoomKind Kind = ERoomKind::Reception;
        FVector Origin = FVector::ZeroVector;
        float ExitY = 0;
        TArray<AActor*> Geometry;
        AActor* Door = nullptr;
        TArray<UStaticMeshComponent*> DoorIndicators;
    };
    void BuildRoom(FRoom& Room);
    void ActivateRoom();
    void PrepareNextRoom();
    void StageNextRoom();
    void AdvanceRoom();
    void BuildPickups();
    void SpawnDrone(FDrone& Drone, const FVector& Point);
    void UpdateDrones(AShadowRunner& Player, double Now, float DeltaSeconds);
    bool CanSee(const FVector& From, const AActor& Target, const AActor* Ignore) const;
    void Notice(const TCHAR* Text);
    void LoadProfile();
    bool SaveProfile();
    void ApplyBuild(AShadowRunner& Player, bool bRefill) const;
    bool IsWorkbenchRoom() const;
    int32& UpgradeLevel(int32 Index) const;
    void SetMenu(EShadowMenu Page);
    void RefreshMenuInput();
    void ApplyVolume() const;

    UPROPERTY()
    TObjectPtr<UShadowProfile> Profile;
    FString ProfileSlot;
    bool bShopOpen = false;
    int32 ShopSelection = 0;
    EShadowMenu Menu = EShadowMenu::None;
    EShadowMenu ParentMenu = EShadowMenu::Main;
    int32 MenuSelection = 0;
    int32 Sensitivity = 100;
    int32 Volume = 100;
    UPROPERTY()
    TObjectPtr<AActor> MainMenuCamera;
    bool bPerkBoughtThisVisit = false;
    int32 CombatPerks = 0;
    int32 DefensePerks = 0;
    int32 HandlingPerks = 0;

    TArray<FPickup> Pickups;
    TArray<FDrone> Drones;
    TArray<FDrone> StagedDrones;
    TArray<FVector> SpawnPoints;
    TArray<FVector> StagedSpawnPoints;
    TArray<AActor*> PortalBarrier;
    AActor* PortalShotBlocker = nullptr;
    bool bNextStaged = false;
    TArray<TWeakObjectPtr<AActor>> ShotActors;
    FRoom Current;
    FRoom Next;
    FRandomStream RoomRandom;
    bool bNextReady = false;
    int32 RoomsThisFloor = 0;
    AActor* CarbinePickup = nullptr;
    float GateHeight = 0;
};

UCLASS()
class AShadowPrototypeHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    virtual void DrawHUD() override;
    virtual void NotifyHitBoxClick(FName BoxName) override;
    void UpdatePointer(FVector2D Point, bool bPressed);

private:
    void DrawMenu(const AShadowPrototype& Mode, const AShadowRunner& Player);
    bool RegisterButton(FName Name, FVector2D Position, FVector2D Size);
    void DrawMenuLabel(const FString& Label, FLinearColor Color, FVector2D At, float Size, bool bTitle = false);
    void DrawButtonSurface(FName Name, FVector2D At, FVector2D Size, bool bHighlighted, bool bEnabled = true);
    FName PressedButton;
    UPROPERTY()
    TObjectPtr<UFont> MenuTitleFont;
    UPROPERTY()
    TObjectPtr<UFont> MenuBodyFont;
    UPROPERTY()
    TObjectPtr<UTexture2D> ButtonNormal;
    UPROPERTY()
    TObjectPtr<UTexture2D> ButtonFocus;
    UPROPERTY()
    TObjectPtr<UTexture2D> ButtonPressed;
    UPROPERTY()
    TObjectPtr<UTexture2D> ButtonDisabled;
    UPROPERTY()
    TObjectPtr<UTexture2D> PausePanel;
    UPROPERTY()
    TObjectPtr<UTexture2D> Crosshair;
    UPROPERTY()
    TObjectPtr<UTexture2D> HitMarker;
    UPROPERTY()
    TObjectPtr<UTexture2D> ShopAmmoIcon;
    UPROPERTY()
    TObjectPtr<UTexture2D> ShopHealthIcon;
    UPROPERTY()
    TObjectPtr<UTexture2D> ShopShieldIcon;
    UPROPERTY()
    TObjectPtr<UTexture2D> ShopReloadIcon;
};
