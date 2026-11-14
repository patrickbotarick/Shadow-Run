#include "ShadowPrototype.h"
#include "ShadowProfile.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/PointLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

namespace
{
    constexpr float RoomLength = 1400.f;
    constexpr float RoomWidth = 1000.f;
    constexpr float PortalHalfWidth = 145.f;
    constexpr float PortalCrossingDepth = 80.f;
    constexpr float PortalJambInset = 29.f;
    constexpr float PortalOpeningHeight = 210.f;
    constexpr float PortalFieldHeight = PortalOpeningHeight - 4.f;
    constexpr float PortalFieldHalfWidth = PortalHalfWidth - 2.f;
    constexpr float DroneRadius = 74.f;
    constexpr float DroneMeshYaw = 180.f;
    constexpr float WorkshopRise = 144.f;
    constexpr int32 MagazineSize = 24;
    constexpr int32 ReserveLimit = 96;
    constexpr float ReloadDuration = 1.38f;
    constexpr double ShotInterval = .12;
    constexpr int32 PistolCapacity = 12;
    constexpr int32 PistolReserveLimit = 48;
    constexpr float PistolReloadDuration = 1.05f;
    constexpr double PistolShotInterval = .24;
    const FVector Entry(220, 0, 98);
    const FLinearColor Cyan(.13f, .86f, .94f);
    const FLinearColor Amber(1.f, .65f, .23f);

    template <typename T>
    T* Asset(const FString& Path)
    {
        return Cast<T>(StaticLoadObject(T::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn));
    }

    UStaticMesh* Model(const TCHAR* Name)
    {
        return Asset<UStaticMesh>(FString::Printf(TEXT("/Game/ShadowRun/Models/%s/%s.%s"), Name, Name, Name));
    }

    UStaticMesh* Cube()
    {
        return Asset<UStaticMesh>(TEXT("/Engine/BasicShapes/Cube.Cube"));
    }

    AStaticMeshActor* Block(UWorld* World, const FVector& At, const FVector& Size, UStaticMesh* Mesh = nullptr)
    {
        auto* Actor = World->SpawnActor<AStaticMeshActor>(At, FRotator::ZeroRotator);
        auto* Component = Actor->GetStaticMeshComponent();
        Component->SetMobility(EComponentMobility::Movable);
        Component->SetStaticMesh(Mesh ? Mesh : Cube());
        const FBox Bounds = Component->GetStaticMesh()->GetBoundingBox();
        const FVector Scale = Size / Bounds.GetSize().ComponentMax(FVector(1));
        Component->SetWorldScale3D(Scale);
        Actor->SetActorLocation(At - Bounds.GetCenter() * Scale);
        return Actor;
    }

    APointLight* Lamp(UWorld* World, const FVector& At, const FLinearColor& Tint, float Power, float Range)
    {
        auto* Actor = World->SpawnActor<APointLight>(At, FRotator::ZeroRotator);
        auto* Light = CastChecked<UPointLightComponent>(Actor->GetLightComponent());
        Light->SetMobility(EComponentMobility::Movable);
        Light->SetLightColor(Tint);
        Light->SetIntensity(Power);
        Light->SetAttenuationRadius(Range);
        Light->SetSourceRadius(22.f);
        return Actor;
    }

    AShadowPrototype* Run(const AActor* Actor)
    {
        return Actor->GetWorld()->GetAuthGameMode<AShadowPrototype>();
    }
}

AShadowRunner::AShadowRunner()
{
    PrimaryActorTick.bCanEverTick = true;
    bUseControllerRotationYaw = true;
    GetCharacterMovement()->MaxWalkSpeed = 470;
    GetCharacterMovement()->JumpZVelocity = 400;
    Eyes = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
    Eyes->SetupAttachment(GetCapsuleComponent());
    Eyes->SetRelativeLocation(FVector(0, 0, 62));
    Eyes->bUsePawnControlRotation = true;
    Rifle = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Carbine"));
    Rifle->SetForceDisableNanite(true);
    Rifle->SetupAttachment(Eyes);
    Rifle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Rifle->SetCastShadow(false);
    MuzzleFlash = CreateDefaultSubobject<UPointLightComponent>(TEXT("MuzzleFlash"));
    MuzzleFlash->SetupAttachment(Rifle);
    MuzzleFlash->SetLightColor(FLinearColor(1.f, .44f, .12f));
    MuzzleFlash->SetIntensity(3200.f);
    MuzzleFlash->SetAttenuationRadius(240.f);
    MuzzleFlash->SetCastShadows(false);
    MuzzleFlash->SetVisibility(false);
}

void AShadowRunner::BeginPlay()
{
    Super::BeginPlay();
    Rifle->SetVisibility(false);
}

void AShadowRunner::CollectCarbine()
{
    bCarbineCollected = true;
    Rifle->SetVisibility(true);
    EquipCarbine();
}

bool AShadowRunner::SelectShop(int32 Index)
{
    auto* Mode = Run(this);
    return Mode && Mode->SelectShopCard(Index);
}

void AShadowRunner::ConfirmShop()
{
    if (auto* Mode = Run(this))
    {
        if (Mode->IsShopOpen())
            Mode->TryShopChoice(*this, Mode->GetShopSelection());
        else
            Mode->ConfirmMenu();
    }
}

void AShadowRunner::EquipCarbine() { if (!SelectShop(0)) EquipWeapon(false); }
void AShadowRunner::EquipPistol() { if (!SelectShop(1)) EquipWeapon(true); }
void AShadowRunner::ShopThree() { SelectShop(2); }
void AShadowRunner::ShopFour() { SelectShop(3); }

void AShadowRunner::EquipWeapon(bool bPistol)
{
    if (!bCarbineCollected)
        return;
    if (const auto* Mode = Run(this); Mode && Mode->bFinished)
        return;
    UStaticMesh* WeaponMesh = bPistol
        ? Asset<UStaticMesh>(TEXT("/Game/ShadowRun/Weapons/Pistol/SM_TestPistol.SM_TestPistol"))
        : Model(TEXT("futuristic_gun_3d_model"));
    if (!WeaponMesh || (Rifle->GetStaticMesh() && bPistolEquipped == bPistol))
        return;

    if (bPistolEquipped)
    {
        PistolMagazine = Magazine;
        PistolReserve = Reserve;
    }
    else
    {
        CarbineMagazine = Magazine;
        CarbineReserve = Reserve;
    }
    StopFire();
    StopAim();
    GetWorldTimerManager().ClearTimer(ReloadTimer);
    Recoil = 0;
    FlashRemaining = 0;
    MuzzleFlash->SetVisibility(false);
    bPistolEquipped = bPistol;
    Magazine = bPistol ? PistolMagazine : CarbineMagazine;
    Reserve = bPistol ? PistolReserve : CarbineReserve;
    Rifle->EmptyOverrideMaterials();
    Rifle->SetStaticMesh(WeaponMesh);
    auto* Finish = Asset<UMaterialInterface>(bPistol ? TEXT("/Game/ShadowRun/Weapons/M_PistolGunmetal.M_PistolGunmetal")
        : TEXT("/Game/ShadowRun/Weapons/M_CarbineGunmetal.M_CarbineGunmetal"));
    if (Finish)
        for (int32 Slot = 0; Slot < Rifle->GetNumMaterials(); ++Slot)
            Rifle->SetMaterial(Slot, Finish);
    const FBox Bounds = WeaponMesh->GetBoundingBox();
    RifleCenter = Bounds.GetCenter();
    RifleScale = bPistol ? 35 / FMath::Max(1.0, Bounds.GetSize().Y)
                         : 61 / FMath::Max(1.0, Bounds.GetSize().X);
    // The pistol's muzzle is at +Y in Unreal; the carbine's is at -X.
    WeaponYaw = bPistol ? -90.f : 180.f;
    MuzzleLocal = bPistol ? FVector(RifleCenter.X, Bounds.Max.Y, RifleCenter.Z + Bounds.GetExtent().Z * .67)
                          : FVector(Bounds.Min.X, RifleCenter.Y, RifleCenter.Z + Bounds.GetExtent().Z * .45);
    MuzzleFlash->SetRelativeLocation(MuzzleLocal);
    UpdateWeaponPose();
}

void AShadowRunner::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    Input->BindAxis(TEXT("SRForward"), this, &AShadowRunner::Forward);
    Input->BindAxis(TEXT("SRStrafe"), this, &AShadowRunner::Strafe);
    Input->BindAxis(TEXT("SRTurn"), this, &AShadowRunner::LookYaw);
    Input->BindAxis(TEXT("SRLook"), this, &AShadowRunner::LookPitch);
    Input->BindAction(TEXT("SRFire"), IE_Pressed, this, &AShadowRunner::StartFire);
    Input->BindAction(TEXT("SRFire"), IE_Released, this, &AShadowRunner::StopFire).bExecuteWhenPaused = true;
    Input->BindAction(TEXT("SRAim"), IE_Pressed, this, &AShadowRunner::StartAim);
    Input->BindAction(TEXT("SRAim"), IE_Released, this, &AShadowRunner::StopAim).bExecuteWhenPaused = true;
    Input->BindAction(TEXT("SRCarbine"), IE_Pressed, this, &AShadowRunner::EquipCarbine);
    Input->BindAction(TEXT("SRPistol"), IE_Pressed, this, &AShadowRunner::EquipPistol);
    Input->BindAction(TEXT("SRShopThree"), IE_Pressed, this, &AShadowRunner::ShopThree);
    Input->BindAction(TEXT("SRShopFour"), IE_Pressed, this, &AShadowRunner::ShopFour);
    Input->BindAction(TEXT("SRShopConfirm"), IE_Pressed, this, &AShadowRunner::ConfirmShop).bExecuteWhenPaused = true;
    Input->BindAction(TEXT("SRReload"), IE_Pressed, this, &AShadowRunner::Reload);
    Input->BindAction(TEXT("SRInteract"), IE_Pressed, this, &AShadowRunner::Interact);
    Input->BindAction(TEXT("SRJump"), IE_Pressed, this, &ACharacter::Jump);
    Input->BindAction(TEXT("SRJump"), IE_Released, this, &ACharacter::StopJumping);
    Input->BindAction(TEXT("SRPause"), IE_Pressed, this, &AShadowRunner::Pause).bExecuteWhenPaused = true;
    Input->BindAction(TEXT("SRRestart"), IE_Pressed, this, &AShadowRunner::Restart).bExecuteWhenPaused = true;
}

void AShadowRunner::Forward(float Value) { AddMovementInput(GetActorForwardVector(), Value); }
void AShadowRunner::Strafe(float Value) { AddMovementInput(GetActorRightVector(), Value); }
void AShadowRunner::LookYaw(float Value) { if (const auto* Mode = Run(this)) AddControllerYawInput(Value * Mode->GetSensitivity() / 100.f); }
void AShadowRunner::LookPitch(float Value) { if (const auto* Mode = Run(this)) AddControllerPitchInput(Value * Mode->GetSensitivity() / 100.f); }

void AShadowRunner::StartFire()
{
    bFireHeld = true;
    Fire();
}

void AShadowRunner::StopFire()
{
    bFireHeld = false;
    bDryFirePlayed = false;
}

void AShadowRunner::StartAim() { bAimHeld = true; }
void AShadowRunner::StopAim() { bAimHeld = false; }

void AShadowRunner::Fire()
{
    auto* Mode = Run(this);
    const double Now = GetWorld()->GetTimeSeconds();
    if (!bCarbineCollected || !Mode || Mode->bFinished || Mode->IsShopOpen() || IsReloading() || Now < NextShot)
        return;

    if (Magazine == 0)
    {
        if (!bDryFirePlayed)
        {
            Mode->Sound(TEXT("SFX_SR_Tiro_Seco"));
            bDryFirePlayed = true;
        }
        return;
    }

    NextShot = Now + (bPistolEquipped ? PistolShotInterval : ShotInterval);
    --Magazine;
    const FVector Origin = Eyes->GetComponentLocation();
    const FVector Direction = Eyes->GetForwardVector();
    const FVector Muzzle = Rifle->GetStaticMesh() ? Rifle->GetComponentTransform().TransformPosition(MuzzleLocal)
                                                 : Origin + Direction * 45;
    Mode->Fire(*this, Origin, Direction, Muzzle);
    Recoil = FMath::Min(Recoil + (bPistolEquipped ? 1.3f : 1.f), 1.8f);
    AddControllerPitchInput(-FMath::Lerp(.62f, .32f, AimBlend));
    FlashRemaining = .048f;
    MuzzleFlash->SetVisibility(true);
}

void AShadowRunner::Reload()
{
    auto* Mode = Run(this);
    if (!bCarbineCollected || !Mode || Mode->bFinished || Mode->IsShopOpen() || Magazine == (bPistolEquipped ? PistolCapacity : MagazineSize) || Reserve == 0 || IsReloading())
        return;

    Mode->Sound(TEXT("SFX_SR_Recarga"));
    GetWorldTimerManager().SetTimer(ReloadTimer, this, &AShadowRunner::FinishReload,
        GetReloadDuration(), false);
}

float AShadowRunner::GetReloadDuration() const
{
    return (bPistolEquipped ? PistolReloadDuration : ReloadDuration) * ReloadMultiplier;
}

float AShadowRunner::GetReloadProgress() const
{
    return IsReloading() ? FMath::Clamp(1.f - GetWorldTimerManager().GetTimerRemaining(ReloadTimer) / GetReloadDuration(), 0.f, 1.f) : 0.f;
}

void AShadowRunner::FinishReload()
{
    ReloadTimer.Invalidate();
    const int32 Rounds = FMath::Min((bPistolEquipped ? PistolCapacity : MagazineSize) - Magazine, Reserve);
    Magazine += Rounds;
    Reserve -= Rounds;
    bDryFirePlayed = false;
}

void AShadowRunner::Interact()
{
    if (auto* Mode = Run(this); Mode && !Mode->bFinished)
        Mode->Interact(*this);
}

void AShadowRunner::Pause()
{
    if (auto* Mode = Run(this); Mode && !Mode->bFinished)
        Mode->ToggleMenu();
}

void AShadowRunner::ReleaseCombatInput()
{
    StopFire();
    StopAim();
    StopJumping();
    FlashRemaining = 0;
    MuzzleFlash->SetVisibility(false);
}

void AShadowRunner::Restart()
{
    const auto* Mode = Run(this);
    if (Mode && (Mode->bFinished || Mode->GetMenu() == EShadowMenu::Pause))
        UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this)), true, TEXT("ShadowDirectPlay"));
}

void AShadowRunner::HitByDrone(int32 Damage)
{
    if (auto* Mode = Run(this); Mode && !Mode->bFinished)
    {
        const int32 Absorbed = FMath::Min(Shield, Damage);
        Shield -= Absorbed;
        Health = FMath::Max(0, Health - (Damage - Absorbed));
        Mode->Sound(TEXT("SFX_SR_Jogador_Dano"), .45f);
        if (Health == 0)
            Mode->Finish(false);
    }
}

void AShadowRunner::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bFireHeld && !bPistolEquipped)
        Fire();

    Recoil = FMath::FInterpTo(Recoil, 0.f, DeltaSeconds, 15.f);
    FlashRemaining = FMath::Max(0.f, FlashRemaining - DeltaSeconds);
    MuzzleFlash->SetVisibility(FlashRemaining > 0.f);
    const auto* Mode = Run(this);
    const float AimTarget = bAimHeld && !IsReloading() && Mode && !Mode->bFinished ? 1.f : 0.f;
    AimBlend = FMath::FInterpTo(AimBlend, AimTarget, DeltaSeconds, 13.f);
    Eyes->SetFieldOfView(FMath::Lerp(90.f, 72.f, AimBlend));
    UpdateWeaponPose();
}

void AShadowRunner::UpdateWeaponPose()
{
    const float ReloadDip = FMath::Sin(PI * GetReloadProgress());
    const FRotator Rotation(3.f * Recoil - 25.f * ReloadDip, WeaponYaw, 12.f * ReloadDip);
    const FVector Hip = bPistolEquipped ? FVector(50, 17, -22) : FVector(59, 22, -24);
    const FVector Aimed = bPistolEquipped ? FVector(54, 0, -12) : FVector(67, 0, -14);
    const FVector Center = FMath::Lerp(Hip, Aimed, AimBlend)
        + FVector(-4.f * Recoil, 0, -12.f * ReloadDip);
    Rifle->SetRelativeTransform(FTransform(Rotation,
        Center - Rotation.RotateVector(RifleCenter * RifleScale), FVector(RifleScale)));
}

AShadowPrototype::AShadowPrototype()
{
    PrimaryActorTick.bCanEverTick = true;
    DefaultPawnClass = AShadowRunner::StaticClass();
    HUDClass = AShadowPrototypeHUD::StaticClass();
    PlayerControllerClass = AShadowPlayerController::StaticClass();
}

#if WITH_EDITOR
void StartShadowControlsCheck(AShadowPrototype& Mode);
#endif

void AShadowPrototype::StartPlay()
{
    LoadProfile();
    int32 Seed = FMath::Rand();
    FParse::Value(FCommandLine::Get(), TEXT("ShadowSeed="), Seed);
    RoomRandom.Initialize(Seed);
    FString RequestedDebugModule;
    FString RequestedDebugVariant;
    const bool bHasDebugModule = FParse::Value(FCommandLine::Get(), TEXT("ShadowDebugModule="), RequestedDebugModule);
    const bool bHasDebugVariant = FParse::Value(FCommandLine::Get(), TEXT("ShadowDebugVariant="), RequestedDebugVariant);
    bDebugModuleRequested = bHasDebugModule || bHasDebugVariant;
    if (bDebugModuleRequested)
    {
        bDebugModuleValid = bHasDebugModule && bHasDebugVariant;
        DebugModuleName = RequestedDebugModule;
        if (RequestedDebugModule == TEXT("Combat.Cargo")) DebugRoomKind = ERoomKind::Cargo;
        else if (RequestedDebugModule == TEXT("Combat.MachineRing")) DebugRoomKind = ERoomKind::Machines;
        else if (RequestedDebugModule == TEXT("Combat.Crossroads")) DebugRoomKind = ERoomKind::Crossroads;
        else if (RequestedDebugModule == TEXT("Combat.Substation")) DebugRoomKind = ERoomKind::Substation;
        else if (RequestedDebugModule == TEXT("Combat.Drainage")) DebugRoomKind = ERoomKind::Drainage;
        else if (RequestedDebugModule == TEXT("Combat.DoubleGallery")) DebugRoomKind = ERoomKind::DoubleGallery;
        else bDebugModuleValid = false;

        if (RequestedDebugVariant == TEXT("A")) DebugVariant = 0;
        else if (RequestedDebugVariant == TEXT("B")) DebugVariant = 1;
        else bDebugModuleValid = false;

        UE_LOG(LogTemp, Display, TEXT("SHADOW_DEBUG_REQUEST module=%s variant=%s valid=%d"),
            *RequestedDebugModule, *RequestedDebugVariant, bDebugModuleValid);
        if (!bDebugModuleValid)
            UE_LOG(LogTemp, Error, TEXT("SHADOW_DEBUG_INVALID module=%s variant=%s; directed selection disabled and test must fail"),
                *RequestedDebugModule, *RequestedDebugVariant);
    }
    if (GConfig)
        GConfig->GetInt(TEXT("ShadowRun.Rooms"), TEXT("CombatRoomsPerFloor"), CombatRoomsPerFloorSetting, GGameIni);
    CombatRoomsPerFloorSetting = FMath::Clamp(CombatRoomsPerFloorSetting, 1, 6);
    Current.CellsNeeded = CellsForRoom(Current.Kind, FloorNumber);
    Current.Variant = 0;
    ConfigureRoom(Current);
    BuildRoom(Current);
    ActivateRoom();
    if (FParse::Param(FCommandLine::Get(), TEXT("ShadowValidateModule")) && bDebugModuleRequested)
        ValidateDebugModule(Next);
    Super::StartPlay();
    if (auto* PC = GetWorld()->GetFirstPlayerController())
    {
        if (!PC->GetPawn())
            RestartPlayerAtTransform(PC, FTransform(FRotator::ZeroRotator, Entry));
        if (auto* Runner = Cast<AShadowRunner>(PC->GetPawn()))
        {
            ApplyBuild(*Runner, true);
            Runner->SetActorLocationAndRotation(Entry, FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);
            Runner->GetCharacterMovement()->SetDefaultMovementMode();
        }
        PC->SetControlRotation(FRotator::ZeroRotator);
    }
    if (GConfig)
    {
        GConfig->GetInt(TEXT("ShadowRun.Controls"), TEXT("Sensitivity"), Sensitivity, GGameUserSettingsIni);
        GConfig->GetInt(TEXT("ShadowRun.Controls"), TEXT("Volume"), Volume, GGameUserSettingsIni);
        Sensitivity = FMath::Clamp(Sensitivity, 20, 200);
        Volume = FMath::Clamp(Volume, 0, 100);
    }
    ApplyVolume();
    SetMenu(UGameplayStatics::HasOption(OptionsString, TEXT("ShadowDirectPlay")) ? EShadowMenu::None : EShadowMenu::Main);
    UE_LOG(LogTemp, Display, TEXT("SHADOW_RUN_READY: reception seed=%d"), Seed);
#if WITH_EDITOR
    StartShadowControlsCheck(*this);
#endif
}

bool AShadowPrototype::IsCombatRoom(ERoomKind Kind) const
{
    return Kind != ERoomKind::Reception && Kind != ERoomKind::Workshop;
}

AShadowPrototype::ERoomKind AShadowPrototype::SelectCombatRoomKind()
{
    if (bDebugModuleRequested && !bDebugModuleConsumed && bDebugModuleValid)
        return DebugRoomKind;
    const ERoomKind Kinds[] = {ERoomKind::Cargo, ERoomKind::Machines, ERoomKind::Crossroads,
                               ERoomKind::Substation, ERoomKind::Drainage, ERoomKind::DoubleGallery};
    int32 Index = RoomRandom.RandRange(0, UE_ARRAY_COUNT(Kinds)-1);
    if (IsCombatRoom(Current.Kind) && Kinds[Index] == Current.Kind)
        Index = (Index + 1) % UE_ARRAY_COUNT(Kinds);
    return Kinds[Index];
}

void AShadowPrototype::ConfigureRoom(FRoom& Room) const
{
    Room.ModuleId = NAME_None;
    Room.EntryTransform = FTransform(FRotator::ZeroRotator, FVector(0,0,98));
    Room.ExitTransforms.Reset();
    Room.CellSpawnPoints.Reset();
    Room.DroneSpawnPoints.Reset();
    Room.Occupancy = FBox(FVector(0,-520,0), FVector(RoomLength,520,378));

    const float ExitValues[] = {-260.f, 0.f, 260.f};
    for (const float Exit : ExitValues)
        Room.ExitTransforms.Add(FTransform(FRotator::ZeroRotator, FVector(RoomLength,Exit,0)));

    switch (Room.Kind)
    {
    case ERoomKind::Reception:
        Room.ModuleId = TEXT("Reception.Fixed");
        Room.ExitTransforms.SetNum(1);
        Room.ExitTransforms[0] = FTransform(FRotator::ZeroRotator, FVector(RoomLength,0,0));
        Room.CellSpawnPoints = {FVector(640,0,49)};
        break;
    case ERoomKind::Cargo:
        Room.ModuleId = Room.Variant == 0 ? TEXT("Combat.Depot.A") : TEXT("Combat.Depot.B");
        Room.CellSpawnPoints = Room.Variant == 0
            ? TArray<FVector>{{565,-300,49},{850,275,49},{1160,-225,49}}
            : TArray<FVector>{{500,260,49},{760,-280,49},{1130,210,49}};
        Room.DroneSpawnPoints = {{560,-285,220},{790,325,220},{1130,-320,220},{1150,95,220}};
        break;
    case ERoomKind::Machines:
        Room.ModuleId = Room.Variant == 0 ? TEXT("Combat.MachineRing.A") : TEXT("Combat.MachineRing.B");
        Room.CellSpawnPoints = Room.Variant == 0
            ? TArray<FVector>{{520,-300,49},{780,300,49},{1120,-230,49}}
            : TArray<FVector>{{500,270,49},{760,-300,49},{1120,210,49}};
        Room.DroneSpawnPoints = {{500,300,220},{760,-300,220},{1030,300,220},{1160,-120,220}};
        break;
    case ERoomKind::Crossroads:
        Room.ModuleId = Room.Variant == 0 ? TEXT("Combat.Crossroads.A") : TEXT("Combat.Crossroads.B");
        Room.CellSpawnPoints = Room.Variant == 0
            ? TArray<FVector>{{470,-300,49},{720,0,49},{1060,300,49}}
            : TArray<FVector>{{500,300,49},{760,0,49},{1100,-300,49}};
        Room.DroneSpawnPoints = {{470,-300,220},{700,300,220},{930,-300,220},{1160,300,220}};
        break;
    case ERoomKind::Substation:
        Room.ModuleId = Room.Variant == 0 ? TEXT("Combat.Substation.A") : TEXT("Combat.Substation.B");
        Room.CellSpawnPoints = Room.Variant == 0
            ? TArray<FVector>{{500,-300,49},{820,260,79},{1110,-210,49}}
            : TArray<FVector>{{520,270,49},{800,-260,79},{1120,230,49}};
        Room.DroneSpawnPoints = {{500,280,220},{760,-300,250},{1000,280,220},{1160,-220,220}};
        break;
    case ERoomKind::Drainage:
        Room.ModuleId = Room.Variant == 0 ? TEXT("Combat.Drainage.A") : TEXT("Combat.Drainage.B");
        Room.CellSpawnPoints = Room.Variant == 0
            ? TArray<FVector>{{470,-300,49},{760,240,49},{1090,-260,49}}
            : TArray<FVector>{{500,280,49},{780,-230,49},{1130,260,49}};
        Room.DroneSpawnPoints = {{450,-300,220},{700,300,220},{950,-300,220},{1160,250,220}};
        break;
    case ERoomKind::DoubleGallery:
        Room.ModuleId = Room.Variant == 0 ? TEXT("Combat.DoubleGallery.A") : TEXT("Combat.DoubleGallery.B");
        Room.CellSpawnPoints = Room.Variant == 0
            ? TArray<FVector>{{470,-300,49},{820,300,49},{1110,-260,49}}
            : TArray<FVector>{{500,300,49},{800,-300,49},{1140,260,49}};
        Room.DroneSpawnPoints = {{470,300,220},{700,-300,220},{960,300,220},{1160,-250,220}};
        break;
    case ERoomKind::Workshop:
        Room.ModuleId = TEXT("Workshop.Transition");
        Room.ExitTransforms.SetNum(1);
        Room.ExitTransforms[0] = FTransform(FRotator::ZeroRotator, FVector(RoomLength,0,WorkshopRise));
        break;
    }
}

bool AShadowPrototype::ValidateRoomPlacement(const FRoom& Candidate) const
{
    if (!Candidate.Occupancy.IsValid)
        return false;
    const FBox CandidateBounds = Candidate.Occupancy.ShiftBy(Candidate.Origin + FVector(1,0,0));
    if (!Current.Occupancy.IsValid)
        return true;
    return !Current.Occupancy.ShiftBy(Current.Origin).Intersect(CandidateBounds);
}

void AShadowPrototype::ValidateDebugModule(const FRoom& Room) const
{
    UWorld* World = GetWorld();
    const ACharacter* Character = Cast<ACharacter>(DefaultPawnClass ? DefaultPawnClass->GetDefaultObject() : nullptr);
    const UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
    if (!World || !Capsule || Room.ModuleId.IsNone())
    {
        UE_LOG(LogTemp, Error, TEXT("SHADOW_MODULE_VALIDATION result=FAIL reason=missing_world_capsule_or_module"));
        return;
    }

    const float Radius = Capsule->GetScaledCapsuleRadius();
    const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
    TArray<UPrimitiveComponent*> Obstacles;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (!Actor || Actor == Room.Door || !Actor->GetRootComponent()) continue;
        const FVector P = Actor->GetActorLocation();
        if (P.X >= Room.Origin.X-40 && P.X <= Room.Origin.X+RoomLength+40 &&
            P.Y >= Room.Origin.Y-540 && P.Y <= Room.Origin.Y+540)
        {
            TArray<UPrimitiveComponent*> Primitives;
            Actor->GetComponents<UPrimitiveComponent>(Primitives);
            for (UPrimitiveComponent* Primitive : Primitives)
                if (Primitive && Primitive->IsRegistered() && Primitive->IsQueryCollisionEnabled())
                    Obstacles.Add(Primitive);
        }
    }

    auto Walkable = [&](const FVector& P, const AActor* IgnoreActor = nullptr)
    {
        FCollisionQueryParams Q(SCENE_QUERY_STAT(ShadowModuleValidation), false);
        if (IgnoreActor) Q.AddIgnoredActor(IgnoreActor);
        FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, HalfHeight);
        FHitResult Hit;
        const FVector Raised = P + FVector(0,0,HalfHeight+2.f);
        return !World->SweepSingleByChannel(Hit, Raised, Raised, FQuat::Identity, ECC_Pawn, Shape, Q);
    };

    const float Step = FMath::Max(32.f, Radius*0.8f);
    const int32 NX = FMath::CeilToInt(RoomLength/Step);
    const int32 NY = FMath::CeilToInt(1000.f/Step);
    TArray<uint8> Seen; Seen.SetNumZeroed((NX+1)*(NY+1));
    auto Index = [&](int32 X,int32 Y){ return X*(NY+1)+Y; };
    auto Point = [&](int32 X,int32 Y){ return Room.Origin + FVector(FMath::Min(X*Step,RoomLength),-500.f+FMath::Min(Y*Step,1000.f),0); };
    TArray<FIntPoint> Queue;
    const int32 SX=FMath::Clamp(FMath::RoundToInt(180.f/Step),0,NX), SY=FMath::Clamp(FMath::RoundToInt(500.f/Step),0,NY);
    if (Walkable(Point(SX,SY))) { Queue.Add(FIntPoint(SX,SY)); Seen[Index(SX,SY)]=1; }
    for(int32 Head=0; Head<Queue.Num(); ++Head)
    {
        const FIntPoint C=Queue[Head];
        const FIntPoint Dirs[]={FIntPoint(1,0),FIntPoint(-1,0),FIntPoint(0,1),FIntPoint(0,-1)};
        for(const FIntPoint D:Dirs)
        {
            const int32 X=C.X+D.X,Y=C.Y+D.Y;
            if(X<0||X>NX||Y<0||Y>NY||Seen[Index(X,Y)]) continue;
            if(Walkable(Point(X,Y))) { Seen[Index(X,Y)]=1; Queue.Add(FIntPoint(X,Y)); }
        }
    }
    auto Reachable = [&](const FVector& Goal)
    {
        const int32 X=FMath::Clamp(FMath::RoundToInt((Goal.X-Room.Origin.X)/Step),0,NX);
        const int32 Y=FMath::Clamp(FMath::RoundToInt((Goal.Y-Room.Origin.Y+500.f)/Step),0,NY);
        for(int32 DX=-1;DX<=1;++DX) for(int32 DY=-1;DY<=1;++DY)
        { int32 AX=X+DX,AY=Y+DY; if(AX>=0&&AX<=NX&&AY>=0&&AY<=NY&&Seen[Index(AX,AY)]) return true; }
        return false;
    };
    const FVector EntryPoint=Room.Origin+Room.EntryTransform.GetLocation();
    const FVector Terminal=Room.Origin+FVector(1100,300,64);
    int32 ReachableCells=0;
    for(const FVector& Cell:Room.CellSpawnPoints) if(Reachable(Room.Origin+Cell)) ++ReachableCells;
    const bool bTerminal=Reachable(Terminal);
    const bool bExit=Reachable(Room.Origin+FVector(RoomLength-100,Room.ExitY,0));
    int32 Suspicious=0;
    for(int32 I=0;I<Obstacles.Num();++I) for(int32 J=I+1;J<Obstacles.Num();++J)
    {
        const FBox A=Obstacles[I]->Bounds.GetBox(), B=Obstacles[J]->Bounds.GetBox();
        if(A.Intersect(B))
        {
            const FVector OverlapMin(FMath::Max(A.Min.X,B.Min.X),FMath::Max(A.Min.Y,B.Min.Y),FMath::Max(A.Min.Z,B.Min.Z));
            const FVector OverlapMax(FMath::Min(A.Max.X,B.Max.X),FMath::Min(A.Max.Y,B.Max.Y),FMath::Min(A.Max.Z,B.Max.Z));
            const FVector OverlapSize=OverlapMax-OverlapMin;
            if(OverlapSize.GetMin()>4.f)
            {
                ++Suspicious;
                const FVector Mid=(OverlapMin+OverlapMax)*0.5f;
                FCollisionQueryParams PairQuery(SCENE_QUERY_STAT(ShadowModuleOverlap),false);
                PairQuery.AddIgnoredActor(Obstacles[I]->GetOwner());
                PairQuery.AddIgnoredActor(Obstacles[J]->GetOwner());
                FHitResult PlayerHit;
                const bool bBlocksPawn=Obstacles[I]->GetCollisionResponseToChannel(ECC_Pawn)==ECR_Block || Obstacles[J]->GetCollisionResponseToChannel(ECC_Pawn)==ECR_Block;
                const bool bAffectsPlayer=bBlocksPawn && World->OverlapAnyTestByChannel(Mid,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Radius,HalfHeight),PairQuery);
                UE_LOG(LogTemp,Warning,TEXT("SHADOW_MODULE_VALIDATION overlap module=%s variant=%c classification=%s actorA=%s classA=%s componentA=%s actorB=%s classB=%s componentB=%s collisionA=%d collisionB=%d blocksPawn=%d affectsPawn=%d transformA=%s transformB=%s boundsA=%s boundsB=%s penetration=%s position=%s route=%s"),
                    *Room.ModuleId.ToString(),Room.Variant==0?TEXT('A'):TEXT('B'),bAffectsPlayer?TEXT("POTENTIALLY_PROBLEMATIC"):TEXT("INDETERMINATE"),
                    *Obstacles[I]->GetOwner()->GetName(),*Obstacles[I]->GetOwner()->GetClass()->GetName(),*Obstacles[I]->GetName(),
                    *Obstacles[J]->GetOwner()->GetName(),*Obstacles[J]->GetOwner()->GetClass()->GetName(),*Obstacles[J]->GetName(),
                    int32(Obstacles[I]->GetCollisionEnabled()),int32(Obstacles[J]->GetCollisionEnabled()),bBlocksPawn,bAffectsPlayer,
                    *Obstacles[I]->GetComponentTransform().ToHumanReadableString(),*Obstacles[J]->GetComponentTransform().ToHumanReadableString(),*A.ToString(),*B.ToString(),*OverlapSize.ToString(),*Mid.ToString(),Reachable(Mid)?TEXT("GRID_REACHABLE"):TEXT("GRID_UNREACHABLE"));
            }
        }
    }
    UE_LOG(LogTemp,Display,TEXT("SHADOW_MODULE_VALIDATION module=%s variant=%c origin=%s entry=%s exit=%s obstacles=%d overlap_suspects=%d grid_step=%.1f capsule_radius=%.1f capsule_halfheight=%.1f reachable_cells=%d/%d terminal=%s exit=%s"),
        *Room.ModuleId.ToString(),Room.Variant==0?TEXT('A'):TEXT('B'),*Room.Origin.ToString(),*EntryPoint.ToString(),*(Room.Origin+FVector(RoomLength,Room.ExitY,0)).ToString(),Obstacles.Num(),Suspicious,Step,Radius,HalfHeight,ReachableCells,Room.CellSpawnPoints.Num(),bTerminal?TEXT("ACCESSIBLE"):TEXT("BLOCKED"),bExit?TEXT("ACCESSIBLE"):TEXT("BLOCKED"));
    if(Room.ModuleId==TEXT("Combat.Drainage.A"))
    {
        const FVector Prior=FVector(3270,-300,52.5f);
        UE_LOG(LogTemp,Display,TEXT("SHADOW_MODULE_VALIDATION prior_block_world=%s local=%s in_room=%d reachable=%d"),*Prior.ToString(),*(Prior-Room.Origin).ToString(),Room.Occupancy.ShiftBy(Room.Origin).IsInside(Prior),Reachable(Prior));
    }
}

void AShadowPrototype::BuildRoom(FRoom& Room)
{
    auto* World = GetWorld();
    const bool bWorkshop = Room.Kind == ERoomKind::Workshop;
    const bool bSafe = bWorkshop || Room.Kind == ERoomKind::Reception;
    const auto Material = [](const TCHAR* Name) {
        return Asset<UMaterialInterface>(FString::Printf(TEXT("/Game/ShadowRun/Sector/%s.%s"), Name, Name));
    };
    auto* Floor = Material(TEXT("M_SectorFloor"));
    auto* Wall = Material(TEXT("M_SectorWall"));
    auto* Trim = Material(TEXT("M_SectorTrim"));
    auto* CyanStrip = Material(TEXT("M_SectorCyan"));
    auto* AmberStrip = Material(TEXT("M_SectorAmber"));
    const auto Piece = [&](const FVector& At, const FVector& Size, UMaterialInterface* Surface,
                           UStaticMesh* Mesh = nullptr) {
        auto* Actor = Block(World, Room.Origin + At, Size, Mesh);
        Room.Geometry.Add(Actor);
        if (Surface)
            Actor->GetStaticMeshComponent()->SetMaterial(0, Surface);
        return Actor;
    };

    for (int32 Row = 0; Row < 7; ++Row)
    {
        for (int32 Column = 0; Column < 5; ++Column)
            Piece(FVector(100 + Row * 200, -400 + Column * 200, -14), FVector(200,200,28), Floor);
        for (float Side : {-1.f, 1.f})
        {
            Piece(FVector(100 + Row * 200, Side * 510, 180), FVector(200,20,360), Wall);
            auto* Guide = Piece(FVector(100 + Row * 200, Side * 477, 4), FVector(78,6,8),
                                bSafe ? CyanStrip : AmberStrip);
            Guide->SetActorEnableCollision(false);
        }
    }
    Piece(FVector(700,0,369), FVector(1400,1040,18), Trim);
    // Adjacent modules touch at their portal plane; their playable interiors do not overlap.
    for (float Side : {-1.f, 1.f})
    {
        const float Segment = 500 - PortalHalfWidth;
        Piece(FVector(10,Side * (PortalHalfWidth + Segment/2),180), FVector(20,Segment,360), Wall);
    }
    if (Room.Kind == ERoomKind::Reception)
        Piece(FVector(10,0,180), FVector(20,2 * PortalHalfWidth,360), Trim);

    const float PortalBase = bWorkshop ? WorkshopRise : 0;
    const float Exit = Room.ExitY;
    Piece(FVector(RoomLength-10,(-500 + Exit - PortalHalfWidth)/2,180),
          FVector(20,500 + Exit - PortalHalfWidth,360), Wall);
    Piece(FVector(RoomLength-10,(500 + Exit + PortalHalfWidth)/2,180),
          FVector(20,500 - Exit - PortalHalfWidth,360), Wall);
    constexpr float PassageHeight = PortalOpeningHeight;
    const float HeaderBottom = PortalBase + PassageHeight;
    Piece(FVector(RoomLength-24,Exit,(HeaderBottom+360)/2), FVector(56, 2*PortalHalfWidth+24, 360-HeaderBottom), Trim);
    Room.Door = Piece(FVector(RoomLength-19,Exit,PortalBase+PassageHeight/2), FVector(34,2 * PortalHalfWidth+2,PassageHeight), Trim);
    if (auto* DoorMesh = Model(TEXT("industrial_metal_door_3d_model")))
    {
        auto* Visual = Piece(FVector(RoomLength-35,Exit,PortalBase+PassageHeight/2), FVector(24,278,PassageHeight-4), nullptr, DoorMesh);
        Visual->SetActorEnableCollision(false);
        Visual->AttachToActor(Room.Door, FAttachmentTransformRules::KeepWorldTransform);
    }
    for (float Side : {-1.f, 1.f})
    {
        const float JambY = Exit + Side * (PortalHalfWidth + 7);
        Piece(FVector(RoomLength-PortalJambInset,JambY,PortalBase+PassageHeight/2), FVector(54,14,PassageHeight), Trim);
        auto* LampStrip = Piece(FVector(RoomLength-58,JambY,PortalBase+96), FVector(4,7,132), AmberStrip);
        LampStrip->SetActorEnableCollision(false);
        Room.DoorIndicators.Add(LampStrip->GetStaticMeshComponent());
    }

    if (bWorkshop)
    {
        for (int32 Step = 0; Step < 12; ++Step)
        {
            const float Height = (Step+1) * WorkshopRise / 12;
            Piece(FVector(766+Step*33,0,Height/2), FVector(33,320,Height), Floor);
        }
        Piece(FVector(1273,0,WorkshopRise/2), FVector(254,320,WorkshopRise), Floor);
    }
    if (bSafe)
    {
        const FVector Bench = bWorkshop ? FVector(490,-240,52) : FVector(405,-230,52);
        Piece(Bench, FVector(132,85,104), Trim, Model(TEXT("rugged_storage_box_3d_model")));
        Room.Geometry.Add(Lamp(World, Room.Origin + Bench + FVector(0,0,105), Cyan, 1550, 245));
    }
    else
    {
        auto* Crate = Model(TEXT("futuristic_cargo_crate_3d_model"));
        TArray<FVector> Cover;
        if (Room.Kind == ERoomKind::Cargo)
            Cover = Room.Variant == 0
                ? TArray<FVector>{{430,-220,58},{710,210,58},{1000,-90,58}}
                : TArray<FVector>{{390,220,58},{640,-245,58},{930,120,58}};
        else if (Room.Kind == ERoomKind::Machines)
            Cover = Room.Variant == 0
                ? TArray<FVector>{{435,0,76},{695,0,76},{940,160,58},{940,-245,58}}
                : TArray<FVector>{{420,-255,58},{610,175,76},{840,-55,58},{1060,-175,58}};
        for (const FVector& At : Cover)
        {
            auto* Collider = Piece(At, FVector(142,135,At.Z*2), Trim);
            if (Crate)
            {
                Collider->SetActorHiddenInGame(true);
                Piece(At, FVector(150,143,At.Z*2+4), nullptr, Crate)->SetActorEnableCollision(false);
            }
        }

        const auto Decorative = [&](const FVector& At, const FVector& Size, UStaticMesh* Mesh,
                                     UMaterialInterface* Surface = nullptr) {
            auto* Actor = Piece(At, Size, Surface, Mesh);
            Actor->SetActorEnableCollision(false);
            return Actor;
        };
        auto* Device = Model(TEXT("futuristic_device_3d_model"));
        auto* WallPanel = Model(TEXT("industrial_wall_panel_3d_model"));
        switch (Room.Kind)
        {
        case ERoomKind::Machines:
            Decorative(FVector(700,0,92), FVector(210,230,184), Device);
            Piece(FVector(700,-300,36), FVector(220,36,72), Trim);
            Piece(FVector(700,300,36), FVector(220,36,72), Trim);
            break;
        case ERoomKind::Crossroads:
            Decorative(FVector(700,0,76), FVector(150,150,152), Device);
            Piece(FVector(500,-170,58), FVector(120,90,116), Trim);
            Piece(FVector(500,170,58), FVector(120,90,116), Trim);
            Piece(FVector(900,-170,58), FVector(120,90,116), Trim);
            Piece(FVector(900,170,58), FVector(120,90,116), Trim);
            break;
        case ERoomKind::Substation:
            Piece(FVector(690,-250,28), FVector(300,180,56), Trim);
            Decorative(FVector(690,-250,112), FVector(180,105,112), Device);
            Piece(FVector(920,230,44), FVector(250,150,88), Trim);
            Decorative(FVector(920,230,130), FVector(140,90,120), Device);
            break;
        case ERoomKind::Drainage:
            Decorative(FVector(520,-360,94), FVector(420,24,188), WallPanel);
            Decorative(FVector(900,360,94), FVector(420,24,188), WallPanel);
            Piece(FVector(710,-220,24), FVector(360,34,48), Trim);
            Piece(FVector(710,220,24), FVector(360,34,48), Trim);
            break;
        case ERoomKind::DoubleGallery:
            Piece(FVector(700,0,82), FVector(130,470,164), Trim);
            Decorative(FVector(700,0,170), FVector(100,360,86), Device);
            Piece(FVector(430,-300,58), FVector(140,92,116), Trim);
            Piece(FVector(430,300,58), FVector(140,92,116), Trim);
            Piece(FVector(980,-300,58), FVector(140,92,116), Trim);
            Piece(FVector(980,300,58), FVector(140,92,116), Trim);
            break;
        default:
            break;
        }
    }

    const TCHAR* Label = TEXT("SALA DE COMBATE");
    if (Room.Kind == ERoomKind::Reception) Label = TEXT("RECEPÇÃO");
    else if (Room.Kind == ERoomKind::Cargo) Label = TEXT("DEPÓSITO");
    else if (Room.Kind == ERoomKind::Machines) Label = TEXT("ANEL DE MÁQUINAS");
    else if (Room.Kind == ERoomKind::Crossroads) Label = TEXT("CRUZAMENTO");
    else if (Room.Kind == ERoomKind::Substation) Label = TEXT("SUBESTAÇÃO");
    else if (Room.Kind == ERoomKind::Drainage) Label = TEXT("DRENAGEM");
    else if (Room.Kind == ERoomKind::DoubleGallery) Label = TEXT("GALERIA DUPLA");
    else if (Room.Kind == ERoomKind::Workshop) Label = TEXT("OFICINA DE TRANSIÇÃO");
    auto* Sign = World->SpawnActor<AActor>();
    Room.Geometry.Add(Sign);
    auto* Text = NewObject<UTextRenderComponent>(Sign);
    Sign->SetRootComponent(Text);
    Text->SetText(FText::FromString(Label));
    Text->SetHorizontalAlignment(EHTA_Center);
    Text->SetWorldSize(23.f);
    Text->SetTextRenderColor(FColor(102,228,250));
    Text->RegisterComponent();
    Text->SetWorldSize(bWorkshop ? 14.f : 23.f);
    Sign->SetActorLocationAndRotation(Room.Origin + FVector(RoomLength-42,Exit,bWorkshop ? 355 : 270),
                                     FRotator(0,180,0));
    const FLinearColor Tint = bSafe ? FLinearColor(.55f,.85f,1.f)
        : Room.Kind == ERoomKind::Cargo ? FLinearColor(1.f,.66f,.32f)
        : Room.Kind == ERoomKind::Substation ? FLinearColor(1.f,.38f,.20f)
        : Room.Kind == ERoomKind::Drainage ? FLinearColor(.30f,.72f,.85f)
        : Room.Kind == ERoomKind::DoubleGallery ? FLinearColor(.75f,.55f,1.f)
        : FLinearColor(.62f,.77f,1.f);
    for (float X : {260.f, 705.f, 1110.f})
        Room.Geometry.Add(Lamp(World,Room.Origin+FVector(X,0,311),Tint,2650,640));

    if (!bWorkshop)
    {
        Piece(FVector(1100,300,64), FVector(100,85,128), nullptr, Model(TEXT("futuristic_device_3d_model")));
        Room.Geometry.Add(Lamp(World,Room.Origin+FVector(1100,300,176),Cyan,1950,330));
    }
}

void AShadowPrototype::ActivateRoom()
{
    CloseShop();
    bPerkBoughtThisVisit = false;
    bHealBoughtThisVisit = false;
    if (IsWorkshop())
        if (auto* Runner = Cast<AShadowRunner>(UGameplayStatics::GetPlayerPawn(this, 0)))
            Runner->Shield = Runner->MaxShield;
    Carried = 0;
    Deposited = 0;
    GateHeight = 0;
    TerminalPosition = Current.Origin + FVector(1100,300,64);
    if (Current.Kind == ERoomKind::Reception)
    {
        CarbinePickup = Block(GetWorld(), Current.Origin + FVector(345,0,49), FVector(82,25,37),
                              Model(TEXT("futuristic_gun_3d_model")));
        CarbinePickup->SetActorEnableCollision(false);
    }
    BuildPickups();
    PrepareNextRoom();
    if (IsWorkshop())
        StageNextRoom();
}

void AShadowPrototype::PrepareNextRoom()
{
    bNextStaged = false;
    Next = FRoom{};
    const bool bCombat = IsCombatRoom(Current.Kind);
    const bool bFirstCombatSelection = !bDebugModuleConsumed && bDebugModuleRequested;
    if (bDebugModuleRequested && !bDebugModuleValid && bFirstCombatSelection)
    {
        bNextReady = false;
        UE_LOG(LogTemp, Error, TEXT("SHADOW_DEBUG_SELECTION_FAILED reason=invalid_request module=%s"), *DebugModuleName);
        return;
    }
    if (bCombat && RoomsThisFloor + 1 >= CombatRoomsPerFloorSetting)
        Next.Kind = ERoomKind::Workshop;
    else if (bCombat)
        Next.Kind = SelectCombatRoomKind();
    else
        Next.Kind = SelectCombatRoomKind();

    Next.CellsNeeded = CellsForRoom(Next.Kind, FloorNumber);
    const bool bApplyDirected = bFirstCombatSelection && bDebugModuleValid && IsCombatRoom(Next.Kind);
    if (bApplyDirected)
    {
        Next.Kind = DebugRoomKind;
        Next.Variant = DebugVariant;
    }
    else
    {
        Next.Variant = RoomRandom.RandRange(0,1);
        if (Next.Variant == Current.Variant && Next.Kind == Current.Kind)
            Next.Variant = 1 - Current.Variant;
    }

    ConfigureRoom(Next);
    if (bApplyDirected)
        bDebugModuleConsumed = true;
    const int32 ExitIndex = RoomRandom.RandRange(0, Next.ExitTransforms.Num()-1);
    Next.ExitY = Next.ExitTransforms.IsValidIndex(ExitIndex)
        ? Next.ExitTransforms[ExitIndex].GetLocation().Y : 0.f;
    if (Next.Kind == ERoomKind::Workshop)
        Next.ExitY = 0.f;
    Next.Origin = Current.Origin + FVector(RoomLength,Current.ExitY,IsWorkshop() ? WorkshopRise : 0);
    bNextReady = ValidateRoomPlacement(Next);
    if (bNextReady)
    {
        BuildRoom(Next);
        bNextReady = IsValid(Next.Door);
    }
    UE_LOG(LogTemp,Display,TEXT("SHADOW_ROOM_PREPARED kind=%d module=%s variant=%d origin=%s ready=%d"),
        int32(Next.Kind), *Next.ModuleId.ToString(), Next.Variant, *Next.Origin.ToString(), bNextReady);
    if (bApplyDirected && !bNextReady)
    {
        UE_LOG(LogTemp, Error, TEXT("SHADOW_DEBUG_SELECTION_FAILED requested=%s.%s actual=%s ready=0 reason=placement_or_build_validation"),
            *DebugModuleName, DebugVariant == 0 ? TEXT("A") : TEXT("B"), *Next.ModuleId.ToString());
    }
    else if (bApplyDirected)
    {
        UE_LOG(LogTemp, Display, TEXT("SHADOW_DEBUG_SELECTION_OK requested=%s.%s actual=%s ready=1"),
            *DebugModuleName, DebugVariant == 0 ? TEXT("A") : TEXT("B"), *Next.ModuleId.ToString());
    }
}

void AShadowPrototype::StageNextRoom()
{
    if (!bNextReady || bNextStaged)
        return;
    bNextStaged = true;
    if (!IsCombatRoom(Next.Kind))
        return;

    for (const FVector& Local : Next.DroneSpawnPoints)
    {
        const FVector Point = Next.Origin + Local;
        if (DronePathClear(Point, Point + FVector(0,0,1)))
            StagedSpawnPoints.Add(Point);
    }
    StagedDrones.SetNum(FMath::Min(StagedSpawnPoints.Num(), FMath::Min(3, FloorNumber + (IsWorkshop() ? 1 : 0))));
    for (int32 Index = 0; Index < StagedDrones.Num(); ++Index)
    {
        SpawnDrone(StagedDrones[Index], StagedSpawnPoints[Index]);
        StagedDrones[Index].Actor->Tags.Add(TEXT("ShadowStagedDrone"));
    }

    BuildPortalBarrier(Next.Origin - FVector(PortalJambInset,0,0));
}

void AShadowPrototype::BuildPortalBarrier(const FVector& Plane)
{
    auto AddShotBlocker = [&](FVector At, FVector Size, FName Tag) {
        auto* Blocker = Block(GetWorld(), At, Size);
        auto* Collision = Blocker->GetStaticMeshComponent();
        Collision->SetVisibility(false);
        Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
        Collision->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
        Blocker->Tags.Add(Tag);
        PortalBarrier.Add(Blocker);
        return Blocker;
    };
    PortalShotBlocker = AddShotBlocker(Plane+FVector(0,0,PortalOpeningHeight/2),
        FVector(4,2*PortalHalfWidth+4,PortalOpeningHeight+4), TEXT("ShadowShotBarrier"));
    // Guard the gap between the visible field and the point where the player has cleared the doorway.
    AddShotBlocker(Next.Origin+FVector(PortalCrossingDepth,0,180), FVector(4,RoomWidth+24,360), TEXT("ShadowPortalThreshold"));

    PortalPlane = Plane + FVector(0,0,2);
    auto* Quad = Asset<UStaticMesh>(TEXT("/Game/ShadowRun/VFX/Malhas/SM_VFX_Quad.SM_VFX_Quad"));
    auto* FieldMaterial = Asset<UMaterialInterface>(TEXT("/Game/ShadowRun/VFX/M_PortalField.M_PortalField"));
    if (Quad && FieldMaterial)
    {
        auto* Field = Block(GetWorld(), PortalPlane, FVector(PortalFieldHeight,2*PortalFieldHalfWidth,1), Quad);
        Field->SetActorEnableCollision(false);
        Field->SetActorRotation(FRotator(90,0,0));
        Field->SetActorLocation(PortalPlane+FVector(0,0,PortalFieldHeight/2) -
            Field->GetActorRotation().RotateVector(Quad->GetBoundingBox().GetCenter() * Field->GetActorScale3D()));
        Field->GetStaticMeshComponent()->SetCastShadow(false);
        PortalField = Field->GetStaticMeshComponent()->CreateDynamicMaterialInstance(0, FieldMaterial);
        Field->Tags.Add(TEXT("ShadowPortalField"));
        PortalBarrier.Add(Field);
    }
    auto* CyanSurface = Asset<UMaterialInterface>(TEXT("/Game/ShadowRun/Sector/M_SectorCyan.M_SectorCyan"));
    for (float Side : {-1.f, 1.f})
    {
        const double Y = Side*(PortalFieldHalfWidth-2);
        auto* Core = Block(GetWorld(), Plane+FVector(-3,Y,105), FVector(4,4,182));
        Core->SetActorEnableCollision(false);
        Core->GetStaticMeshComponent()->SetMaterial(0, CyanSurface);
        PortalBarrier.Add(Core);
    }
    auto* Sign = GetWorld()->SpawnActor<AActor>();
    auto* Text = NewObject<UTextRenderComponent>(Sign);
    Sign->SetRootComponent(Text);
    Text->SetText(FText::FromString(TEXT("BARREIRA BALÍSTICA")));
    Text->SetHorizontalAlignment(EHTA_Center);
    Text->SetTextRenderColor(FColor(34,230,242));
    Text->SetWorldSize(13.f);
    Text->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Text->RegisterComponent();
    Sign->SetActorLocationAndRotation(Plane+FVector(-25,0,204), FRotator(0,180,0));
    PortalBarrier.Add(Sign);
}

void AShadowPrototype::AdvanceRoom()
{
    if (IsWorkshop())
    {
        ++FloorNumber;
        RoomsThisFloor = 0;
    }
    else if (Current.Kind != ERoomKind::Reception)
    {
        ++RoomsThisFloor;
        ++RoomsCleared;
        Score += 100;
    }
    for (auto& Pickup : Pickups)
        if (IsValid(Pickup.Actor))
            Pickup.Actor->Destroy();
    for (auto& Drone : Drones)
        if (IsValid(Drone.Actor))
            Drone.Actor->Destroy();
    for (auto& Effect : ShotActors)
        if (Effect.IsValid())
            Effect->Destroy();
    for (AActor* Actor : Current.Geometry)
        if (IsValid(Actor))
            Actor->Destroy();
    Pickups.Reset();
    Drones = MoveTemp(StagedDrones);
    SpawnPoints = MoveTemp(StagedSpawnPoints);
    for (auto& Drone : Drones)
    {
        Drone.Actor->Tags.Remove(TEXT("ShadowStagedDrone"));
        Drone.NextAttack = GetWorld()->GetTimeSeconds()+1.3;
    }
    for (auto* Part : PortalBarrier)
        Part->Destroy();
    PortalBarrier.Reset();
    PortalShotBlocker = nullptr;
    PortalField = nullptr;
    ShotActors.Reset();
    Current = MoveTemp(Next);
    auto* RearSeal = Block(GetWorld(),Current.Origin+FVector(11,0,180),FVector(22,2*PortalHalfWidth+2,360));
    if (auto* Surface = Asset<UMaterialInterface>(TEXT("/Game/ShadowRun/Sector/M_SectorTrim.M_SectorTrim")))
        RearSeal->GetStaticMeshComponent()->SetMaterial(0, Surface);
    Current.Geometry.Add(RearSeal);
    // The rear seal belongs to the new room and is released with it.
    UE_LOG(LogTemp,Display,TEXT("SHADOW_ROOM_ENTERED kind=%d floor=%d rooms=%d"),
           int32(Current.Kind),FloorNumber,RoomsCleared);
    ActivateRoom();
}

void AShadowPrototype::BuildPickups()
{
    if (IsWorkshop())
        return;
    const bool bReception = Current.Kind == ERoomKind::Reception;
    const int32 CellCount = bReception ? 1 : FMath::Clamp(GetCellsNeeded(), 1, 3);
    if (Current.CellSpawnPoints.Num() < CellCount)
        ConfigureRoom(Current);
    TArray<int32> Available;
    for (int32 Index = 0; Index < Current.CellSpawnPoints.Num(); ++Index)
        Available.Add(Index);
    for (int32 Index = 0; Index < CellCount; ++Index)
    {
        const int32 Choice = RoomRandom.RandRange(0, Available.Num()-1);
        const FVector CellAt = Current.CellSpawnPoints.IsValidIndex(Available[Choice])
            ? Current.CellSpawnPoints[Available[Choice]] : FVector(640,0,49);
        Available.RemoveAtSwap(Choice);
        auto* Cell = Block(GetWorld(),Current.Origin+CellAt,FVector(45,45,87),Model(TEXT("futuristic_energy_capsule_3d_model")));
        Cell->SetActorEnableCollision(true);
        Cell->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Cell->GetStaticMeshComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
        Cell->GetStaticMeshComponent()->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
        Current.Geometry.Add(Lamp(GetWorld(),Current.Origin+CellAt+FVector(0,0,20),Cyan,1150,180));
        Pickups.Add({Cell,true});
    }
    if (!bReception)
    {
        auto* Ammo = Block(GetWorld(),Current.Origin+FVector(340,260,34),FVector(55,53,48),
                           Model(TEXT("rugged_storage_box_3d_model")));
        Ammo->SetActorEnableCollision(false);
        Pickups.Add({Ammo,false});
    }
}
void AShadowPrototype::SpawnDrone(FDrone& Drone, const FVector& Point)
{
    Drone.Actor = GetWorld()->SpawnActor<AActor>(Point, FRotator(0,180,0));
    Drone.Actor->Tags.Add(TEXT("ShadowDrone"));
    auto* Body = NewObject<UCapsuleComponent>(Drone.Actor);
    Drone.Actor->SetRootComponent(Body);
    Body->InitCapsuleSize(58,60);
    Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Body->SetCollisionResponseToAllChannels(ECR_Ignore);
    Body->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    Body->RegisterComponent();
    Body->SetWorldLocation(Point);
    Body->SetWorldRotation(FRotator(0,180,0));
    auto* Visual = NewObject<UStaticMeshComponent>(Drone.Actor);
    auto* Mesh = Model(TEXT("sci-fi_drone_3d_model"));
    Visual->SetupAttachment(Body);
    Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Visual->SetStaticMesh(Mesh ? Mesh : Cube());
    const FBox Bounds = Visual->GetStaticMesh()->GetBoundingBox();
    const FVector Scale = FVector(118,113,70) / Bounds.GetSize().ComponentMax(FVector(1));
    const FRotator MeshFacing(0,DroneMeshYaw,0);
    Visual->SetRelativeTransform(FTransform(MeshFacing, -MeshFacing.RotateVector(Bounds.GetCenter()*Scale), Scale));
    Visual->RegisterComponent();
    auto* Signal = NewObject<UPointLightComponent>(Drone.Actor);
    Signal->SetupAttachment(Drone.Actor->GetRootComponent());
    Signal->RegisterComponent();
    Signal->SetLightColor(FLinearColor(1.f,.04f,.03f));
    Signal->SetIntensity(1700);
    Signal->SetAttenuationRadius(230);
    Drone.Signal = Signal;
    Drone.PatrolOrigin = Point;
    Drone.PatrolTarget = Point;
    Drone.NextPatrol = 0;
    Drone.AimStarted = 0;
    Drone.bTracking = false;
    Drone.DamageTaken = 0.f;
    Drone.NextAttack = GetWorld()->GetTimeSeconds()+1.3;
    Drone.RespawnAt = 0;
}

bool AShadowPrototype::DronePathClear(const FVector& From, const FVector& To, const AActor* Drone, const AActor* Player) const
{
    FCollisionQueryParams Query;
    Query.AddIgnoredActor(Drone);
    Query.AddIgnoredActor(Player);
    return !GetWorld()->SweepTestByChannel(From, To, FQuat::Identity, ECC_Visibility,
                                           FCollisionShape::MakeSphere(DroneRadius), Query);
}

bool AShadowPrototype::DroneHasShot(const FVector& From, const FVector& To, const AShadowRunner& Player, const AActor* Drone) const
{
    FHitResult Hit;
    FCollisionQueryParams Query;
    Query.AddIgnoredActor(Drone);
    return !GetWorld()->LineTraceSingleByChannel(Hit,From,To,ECC_Visibility,Query) || Hit.GetActor() == &Player;
}

void AShadowPrototype::UpdateDroneFlight(FDrone& Drone, const FRoom& Room, const AShadowRunner& Player, double Now,
                                        float DeltaSeconds, bool bTrackPlayer)
{
    const FVector Position = Drone.Actor->GetActorLocation();
    const FVector ToPlayer = Player.GetPawnViewLocation() - Position;
    const FVector Direction = ToPlayer.GetSafeNormal2D();
    auto InsideRoom = [&Room](FVector Point) {
        Point.X = FMath::Clamp(Point.X, Room.Origin.X+180, Room.Origin.X+RoomLength-110);
        Point.Y = FMath::Clamp(Point.Y, Room.Origin.Y-420, Room.Origin.Y+420);
        Point.Z = Room.Origin.Z + 226;
        return Point;
    };
    if (Now >= Drone.NextPatrol || FVector::DistSquared2D(Position,Drone.PatrolTarget) < FMath::Square(22.f))
    {
        Drone.NextPatrol = Now + 2;
        const float Side = FMath::Sin(Now+Drone.PatrolOrigin.Y) > 0 ? 1.f : -1.f;
        const FVector Offsets[] = {{90,Side*125,0},{-100,Side*120,0},{75,-Side*130,0},
                                   {-80,-Side*125,0},{150,0,0},{-140,0,0}};
        for (const FVector& Offset : Offsets)
        {
            const FVector Candidate = InsideRoom(Drone.PatrolOrigin+Offset);
            if (FVector::DistSquared2D(Position,Candidate)>FMath::Square(45.f) &&
                DronePathClear(Position,Candidate,Drone.Actor,&Player))
            {
                Drone.PatrolTarget = Candidate;
                break;
            }
        }
    }
    FVector Desired = Drone.PatrolTarget;
    if (bTrackPlayer)
    {
        const FVector Side = FVector::CrossProduct(FVector::UpVector, Direction);
        const float Strafe = FMath::Sin(Now * 1.55 + Drone.PatrolOrigin.Y) >= 0 ? 105.f : -105.f;
        const float Distance = ToPlayer.Size2D();
        const float Approach = FMath::Clamp(Distance-460.f,-100.f,125.f);
        for (const FVector& Offset : {Direction*Approach+Side*Strafe, Side*-Strafe, Direction*Approach})
        {
            const FVector Candidate = InsideRoom(Position+Offset);
            if (DronePathClear(Position,Candidate,Drone.Actor,&Player))
            {
                Desired = Candidate;
                break;
            }
        }
    }
    Desired.Z += 10.f*FMath::Sin(Now*2.7+Drone.PatrolOrigin.Y);
    const FVector NextPosition = FMath::VInterpConstantTo(Position,Desired,DeltaSeconds,bTrackPlayer ? 140.f : 105.f);
    if (DronePathClear(Position,NextPosition,Drone.Actor,&Player))
        Drone.Actor->SetActorLocation(NextPosition);
    else
        Drone.NextPatrol = 0;

    const FVector Facing = bTrackPlayer ? ToPlayer : Desired - Position;
    if (Facing.SizeSquared2D() > 16)
    {
        FRotator Aim = Facing.Rotation();
        Aim.Pitch = bTrackPlayer ? FMath::ClampAngle(Aim.Pitch,-60,60) : 0.f;
        Drone.Actor->SetActorRotation(FMath::RInterpConstantTo(Drone.Actor->GetActorRotation(),Aim,DeltaSeconds,185.f));
    }
}

void AShadowPrototype::UpdateDrones(AShadowRunner& Player, double Now, float DeltaSeconds)
{
    const FVector Eye = Player.GetPawnViewLocation();
    const FVector Look = Player.GetViewRotation().Vector();
    for (auto& Drone : StagedDrones)
        UpdateDroneFlight(Drone,Next,Player,Now,DeltaSeconds,false);
    for (auto& Drone : Drones)
    {
        if (!Drone.Actor)
        {
            if (Now < Drone.RespawnAt)
                continue;
            for (const FVector& Point : SpawnPoints)
            {
                if (FVector::DistSquared(Point,Eye) < FMath::Square(650.f) ||
                    !DronePathClear(Point,Point+FVector(0,0,1),nullptr,&Player) ||
                    Drones.ContainsByPredicate([&Point](const FDrone& Other) {
                        return Other.Actor && FVector::DistSquared(Point,Other.PatrolOrigin)<FMath::Square(180.f);
                    }))
                    continue;

                FHitResult Hit;
                FCollisionQueryParams Query;
                Query.AddIgnoredActor(&Player);
                if (FVector::DotProduct(Look,(Point-Eye).GetSafeNormal()) > .05 &&
                    !GetWorld()->LineTraceSingleByChannel(Hit,Eye,Point,ECC_Visibility,Query))
                    continue;
                SpawnDrone(Drone,Point);
                break;
            }
            if (!Drone.Actor)
                Drone.RespawnAt = Now+1;
            continue;
        }

        const FVector Position = Drone.Actor->GetActorLocation();
        const float Distance = FVector::Dist(Position,Eye);
        const bool bDetect = Distance < 920 && DroneHasShot(Position,Eye,Player,Drone.Actor);
        if (bDetect && !Drone.bTracking)
        {
            Drone.AimStarted = Now;
            Sound(TEXT("SFX_SR_Drone_Detectou"),.32f);
        }
        Drone.bTracking = bDetect;
        UpdateDroneFlight(Drone,Current,Player,Now,DeltaSeconds,bDetect);
        Drone.Signal->SetIntensity(bDetect ? 2600+1050*FMath::Sin(Now*12) : 1150);
        const FVector Forward = Drone.Actor->GetActorForwardVector();
        const FVector Emitter = Drone.Actor->GetActorLocation()+Forward*59;
        const FVector Aim = (Eye - Emitter).GetSafeNormal();
        if (bDetect && Distance < 670 && Now >= Drone.NextAttack && Now >= Drone.AimStarted+.42 &&
            FVector::DotProduct(Forward,Aim) >= .985 && DroneHasShot(Emitter,Eye,Player,Drone.Actor))
        {
            Drone.NextAttack = Now+1.8;
            ShotEffects(Emitter,Eye,true,false);
            Sound(TEXT("SFX_SR_Drone_Ataque"),.45f);
            Player.HitByDrone(15);
        }
        if (bFinished)
            return;
    }
}

void AShadowPrototype::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bFinished)
        return;
    auto* Player = Cast<AShadowRunner>(UGameplayStatics::GetPlayerPawn(this,0));
    if (!Player)
        return;
    if (IsShopOpen() && FVector::DistSquared(Player->GetActorLocation(), GetWorkbenchPosition()) > FMath::Square(210.f))
        CloseShop();
    const double Now = GetWorld()->GetTimeSeconds();
    if (PortalField)
        PortalField->SetScalarParameterValue(TEXT("FieldTime"),Now);
    if (IsValid(CarbinePickup) && FVector::DistSquared(Player->GetActorLocation(),CarbinePickup->GetActorLocation()) < FMath::Square(100.f))
    {
        Player->CollectCarbine();
        CarbinePickup->Destroy();
        CarbinePickup = nullptr;
        Notice(TEXT("Carabina equipada"));
    }
    ShotActors.RemoveAll([](const TWeakObjectPtr<AActor>& Actor) { return !Actor.IsValid(); });
    for (auto& Pickup : Pickups)
    {
        if (!Pickup.Actor || FVector::DistSquared(Player->GetActorLocation(),Pickup.Actor->GetActorLocation()) > FMath::Square(106.f))
            continue;
        if (Pickup.bCell)
            continue;
        const int32 AmmoLimit = Player->HasPistolEquipped() ? PistolReserveLimit : ReserveLimit;
        if (Player->Reserve >= AmmoLimit)
            continue;
        Player->Reserve = FMath::Min(AmmoLimit, Player->Reserve + (Player->HasPistolEquipped() ? 12 : 24));
        Sound(TEXT("SFX_SR_Municao_Coletada"));
        Notice(TEXT("Munição coletada"));
        Pickup.Actor->Destroy();
        Pickup.Actor = nullptr;
    }
    if (Deposited == GetCellsNeeded() && bNextReady && GateHeight < 225)
    {
        const float Rise = FMath::Min(225-GateHeight, DeltaSeconds*210);
        Current.Door->AddActorWorldOffset(FVector(0,0,Rise));
        const bool bWasOpen = IsPassageOpen();
        GateHeight += Rise;
        if (!bWasOpen && IsPassageOpen())
            if (auto* CyanSurface = Asset<UMaterialInterface>(TEXT("/Game/ShadowRun/Sector/M_SectorCyan.M_SectorCyan")))
                for (auto* Indicator : Current.DoorIndicators)
                    Indicator->SetMaterial(0, CyanSurface);
    }
    const FVector Local = Player->GetActorLocation() - Next.Origin;
    if (IsPassageOpen() && Local.X >= PortalCrossingDepth && Local.X < 350 &&
        FMath::Abs(Local.Y) < RoomWidth / 2 && Local.Z > 45 && Local.Z < 260)
    {
        AdvanceRoom();
        return;
    }
    UpdateDrones(*Player,Now,DeltaSeconds);
}
void AShadowPrototype::Interact(AShadowRunner& Player)
{
    if (bShopOpen)
    {
        CloseShop();
        return;
    }
    if (AActor* Cell = GetNearbyCell(Player))
    {
        for (auto& Pickup : Pickups)
        {
            if (Pickup.bCell && Pickup.Actor == Cell)
            {
                ++Carried;
                Sound(TEXT("SFX_SR_Celula_Coletada"));
                Notice(TEXT("Célula coletada"));
                Pickup.Actor->Destroy();
                Pickup.Actor = nullptr;
                return;
            }
        }
    }
    if (IsWorkbenchRoom() && FVector::DistSquared(Player.GetActorLocation(), GetWorkbenchPosition()) <= FMath::Square(210.f))
    {
        bShopOpen = true;
        ShopSelection = 0;
        RefreshMenuInput();
        return;
    }
    if (IsWorkshop())
        return;
    if (FVector::Dist(Player.GetActorLocation(),TerminalPosition) > 210)
    {
        if (FMath::Abs(Player.GetActorLocation().X-Current.Origin.X-RoomLength)<200 && Deposited<GetCellsNeeded())
        {
            Sound(TEXT("SFX_SR_Porta_Trancada"));
            Notice(TEXT("Comporta sem energia"));
        }
        return;
    }
    if (Carried == 0)
    {
        Notice(Deposited == GetCellsNeeded() ? TEXT("Comporta aberta") : TEXT("Encontre mais células"));
        return;
    }
    Deposited += Carried;
    Carried = 0;
    Sound(TEXT("SFX_SR_Celula_Encaixada"));
    Notice(TEXT("Energia transferida"));
    if (Deposited == GetCellsNeeded())
    {
        StageNextRoom();
        Sound(TEXT("SFX_SR_Porta_Abrindo"));
        Notice(TEXT("Comporta aberta"));
    }
}

int32 AShadowPrototype::CellsForRoom(ERoomKind Kind, int32 Floor) const
{
    if (Kind == ERoomKind::Workshop)
        return 0;
    if (Kind == ERoomKind::Reception)
        return 1;
    int32 Value = 1;
    const TCHAR* Key = TEXT("CargoCellsFloor1");
    if (Kind == ERoomKind::Machines) Key = TEXT("MachinesCellsFloor1");
    else if (Kind == ERoomKind::Crossroads) Key = TEXT("CrossroadsCellsFloor1");
    else if (Kind == ERoomKind::Substation) Key = TEXT("SubstationCellsFloor1");
    else if (Kind == ERoomKind::Drainage) Key = TEXT("DrainageCellsFloor1");
    else if (Kind == ERoomKind::DoubleGallery) Key = TEXT("DoubleGalleryCellsFloor1");
    if (Floor == 2)
    {
        if (Kind == ERoomKind::Cargo) Key = TEXT("CargoCellsFloor2");
        else if (Kind == ERoomKind::Machines) Key = TEXT("MachinesCellsFloor2");
        else if (Kind == ERoomKind::Crossroads) Key = TEXT("CrossroadsCellsFloor2");
        else if (Kind == ERoomKind::Substation) Key = TEXT("SubstationCellsFloor2");
        else if (Kind == ERoomKind::Drainage) Key = TEXT("DrainageCellsFloor2");
        else if (Kind == ERoomKind::DoubleGallery) Key = TEXT("DoubleGalleryCellsFloor2");
    }
    else if (Floor >= 3)
    {
        if (Kind == ERoomKind::Cargo) Key = TEXT("CargoCellsAdvanced");
        else if (Kind == ERoomKind::Machines) Key = TEXT("MachinesCellsAdvanced");
        else if (Kind == ERoomKind::Crossroads) Key = TEXT("CrossroadsCellsAdvanced");
        else if (Kind == ERoomKind::Substation) Key = TEXT("SubstationCellsAdvanced");
        else if (Kind == ERoomKind::Drainage) Key = TEXT("DrainageCellsAdvanced");
        else if (Kind == ERoomKind::DoubleGallery) Key = TEXT("DoubleGalleryCellsAdvanced");
    }
    if (GConfig)
        GConfig->GetInt(TEXT("ShadowRun.Rooms"), Key, Value, GGameIni);
    return FMath::Clamp(Value, 1, 3);
}

AActor* AShadowPrototype::GetNearbyCell(const AShadowRunner& Player) const
{
    const FVector Eye = Player.GetActorLocation() + FVector(0,0,58);
    const FVector Forward = Player.GetControlRotation().Vector();
    AActor* Best = nullptr;
    double BestDistance = TNumericLimits<double>::Max();
    for (const auto& Pickup : Pickups)
    {
        if (!Pickup.bCell || !IsValid(Pickup.Actor))
            continue;
        const FVector Target = Pickup.Actor->GetActorLocation() + FVector(0,0,35);
        const FVector ToTarget = Target - Eye;
        const double Distance = ToTarget.Size();
        if (Distance > 190 || (Distance > 100 && FVector::DotProduct(Forward, ToTarget.GetSafeNormal()) < .15f))
            continue;
        FHitResult Hit;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(ShadowCellInteract), true);
        Query.AddIgnoredActor(&Player);
        if (GetWorld()->LineTraceSingleByChannel(Hit, Eye, Target, ECC_Visibility, Query) && Hit.GetActor() != Pickup.Actor)
            continue;
        if (Distance < BestDistance)
        {
            BestDistance = Distance;
            Best = Pickup.Actor;
        }
    }
    return Best;
}

FVector AShadowPrototype::GetObjectivePosition(const FVector& PlayerLocation) const
{
    if (Deposited == GetCellsNeeded())
        return Next.Origin + FVector(175,0,150);
    if (Carried + Deposited == GetCellsNeeded())
        return TerminalPosition + FVector(0, 0, 100);

    if (IsValid(CarbinePickup))
        return CarbinePickup->GetActorLocation() + FVector(0,0,55);
    FVector Target = TerminalPosition;
    double Closest = TNumericLimits<double>::Max();
    for (const auto& Pickup : Pickups)
    {
        if (!Pickup.bCell || !Pickup.Actor)
            continue;
        const FVector Position = Pickup.Actor->GetActorLocation();
        const double Distance = FVector::DistSquared(PlayerLocation, Position);
        if (Distance < Closest)
        {
            Closest = Distance;
            Target = Position + FVector(0, 0, 70);
        }
    }
    return Target;
}

void AShadowPrototype::Fire(AShadowRunner& Player, const FVector& Origin, const FVector& Direction, const FVector& Muzzle)
{
    FHitResult Hit;
    FCollisionQueryParams Query;
    Query.AddIgnoredActor(&Player);
    const FVector End = Origin+Direction*9000;
    GetWorld()->LineTraceSingleByChannel(Hit,Origin,End,ECC_Visibility,Query);
    const FVector AimPoint = Hit.bBlockingHit ? Hit.ImpactPoint : End;
    // Check the offset muzzle path so a shot cannot bypass cover at the barrel.
    FHitResult MuzzleHit;
    if (FVector::DotProduct(AimPoint - Muzzle, Direction) > 0 &&
        GetWorld()->LineTraceSingleByChannel(MuzzleHit, Muzzle, AimPoint + Direction * 2, ECC_Visibility, Query))
        Hit = MuzzleHit;
    const bool bFieldHit = PortalShotBlocker && Hit.GetActor() == PortalShotBlocker;
    const bool bBarrierHit = bFieldHit || (Hit.GetActor() && Hit.GetActor()->ActorHasTag(TEXT("ShadowPortalThreshold")));
    ShotEffects(Muzzle, Hit.bBlockingHit ? Hit.ImpactPoint : AimPoint, false, Hit.bBlockingHit, bBarrierHit);
    static const TCHAR* ShotSounds[] = {TEXT("SFX_SR_Tiro_Carabina_01"), TEXT("SFX_SR_Tiro_Carabina_02"),
                                       TEXT("SFX_SR_Tiro_Carabina_03")};
    Sound(ShotSounds[FMath::RandRange(0, 2)], Player.HasPistolEquipped() ? .34f : .48f);
    if (bBarrierHit)
    {
        if (PortalField && bFieldHit)
        {
            const FVector At = Hit.ImpactPoint - PortalPlane;
            PortalField->SetVectorParameterValue(TEXT("Impact"), FLinearColor(
                FMath::Clamp(.5 + At.Y / (2*PortalFieldHalfWidth),0.,1.),
                FMath::Clamp(At.Z / PortalFieldHeight,0.,1.), GetWorld()->GetTimeSeconds(), 1));
        }
        Notice(TEXT("Atravesse a barreira para atacar"));
    }
    if (!Hit.bBlockingHit)
        return;
    for (auto& Drone : Drones)
    {
        if (!Drone.Actor || Drone.Actor != Hit.GetActor())
            continue;
        HitMarkerUntil = GetWorld()->GetTimeSeconds() + .18;
        Drone.DamageTaken += (Player.HasPistolEquipped() ? .8f : 1.f) * Player.DamageMultiplier;
        if (Drone.DamageTaken < 4.f)
        {
            Sound(TEXT("SFX_SR_Drone_Dano"),.48f);
            return;
        }
        Drone.Actor->Destroy();
        Drone.Actor = nullptr;
        Drone.RespawnAt = GetWorld()->GetTimeSeconds()+12;
        ++Kills;
        if (!Drone.bRewarded)
        {
            Drone.bRewarded = true;
            const int32 Reward = FMath::Min(20, 999999 - Profile->Credits);
            Profile->Credits += Reward;
            RunCreditsEarned += Reward;
            Score += 50;
            SaveProfile();
        }
        Sound(TEXT("SFX_SR_Drone_Destruido"));
        Notice(TEXT("Drone destruído"));
        return;
    }
    Sound(TEXT("SFX_SR_Impacto_Metal"),.30f);
}

void AShadowPrototype::Finish(bool bEscaped)
{
    if (bFinished)
        return;
    bFinished = true;
    CloseShop();
    Profile->BestScore = FMath::Max(Profile->BestScore, Score);
    SaveProfile();
    bWon = bEscaped;
    if (auto* PC = GetWorld()->GetFirstPlayerController())
    {
        PC->SetIgnoreMoveInput(true);
        PC->SetIgnoreLookInput(true);
    }
    if (bWon)
        Sound(TEXT("SFX_SR_Fuga_Concluida"));
    UE_LOG(LogTemp, Display, TEXT("SHADOW_RUN_FINISH: %s"), bWon ? TEXT("escaped") : TEXT("defeated"));
}

void AShadowPrototype::Notice(const TCHAR* Text)
{
    Feedback = Text;
    FeedbackUntil = GetWorld()->GetTimeSeconds()+2.2;
}

void AShadowPrototype::Sound(const TCHAR* Name, float Gain) const
{
    if (auto* Wave = Asset<USoundBase>(FString::Printf(TEXT("/Game/ShadowRun/Audio/%s.%s"),Name,Name)))
        UGameplayStatics::PlaySound2D(this,Wave,Gain);
}

void AShadowPrototype::ShotEffects(const FVector& Start, const FVector& End, bool bDrone, bool bImpact, bool bBarrier)
{
    const auto Mesh = [](const TCHAR* Name) {
        return Asset<UStaticMesh>(FString::Printf(TEXT("/Game/ShadowRun/VFX/Malhas/%s.%s"), Name, Name));
    };
    const auto Material = [](const TCHAR* Name) {
        return Asset<UMaterialInterface>(FString::Printf(TEXT("/Game/ShadowRun/Weapons/%s.%s"), Name, Name));
    };
    const FLinearColor Tint = bDrone ? FLinearColor(10.f, .22f, .12f) : FLinearColor(8.f, 3.2f, .66f);
    const auto Effect = [this](UStaticMesh* Shape, UMaterialInterface* Surface, const FVector& Location,
                               const FRotator& Rotation, const FVector& Size, const FLinearColor& Color, float Life) {
        if (!Shape || !Surface)
            return;
        auto* Actor = GetWorld()->SpawnActor<AStaticMeshActor>(Location, Rotation);
        auto* Component = Actor->GetStaticMeshComponent();
        Component->SetMobility(EComponentMobility::Movable);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetCastShadow(false);
        Component->SetStaticMesh(Shape);
        Component->SetWorldScale3D(Size / Shape->GetBoundingBox().GetSize().ComponentMax(FVector(1)));
        if (auto* Instance = Component->CreateDynamicMaterialInstance(0, Surface))
            Instance->SetVectorParameterValue(TEXT("ShotColor"), Color);
        Actor->SetLifeSpan(Life);
        ShotActors.Add(Actor);
    };
    const FVector Shot = End - Start;
    const FRotator CameraFacing = FRotationMatrix::MakeFromZ(Start - End).Rotator();
    auto* Quad = Mesh(TEXT("SM_VFX_Quad"));
    const float FlashSize = bDrone ? 57.f : 26.f;
    Effect(Quad, Material(bDrone ? TEXT("M_DroneFlash") : TEXT("M_ShotFlash")), Start,
           CameraFacing, FVector(FlashSize, FlashSize, 1), Tint, bDrone ? .13f : .07f);
    if (Shot.SizeSquared() > 9)
        Effect(Mesh(TEXT("SM_VFX_Tracante_Cruzado")), Material(TEXT("M_ShotTrace")), (Start + End) * .5,
               Shot.Rotation(), FVector(Shot.Size(), bDrone ? 15 : 7, bDrone ? 15 : 7), Tint, bDrone ? .16f : .085f);
    if (bImpact)
        Effect(Quad, Material(TEXT("M_ShotImpact")), End - Shot.GetSafeNormal() * 3, CameraFacing,
               FVector(42,42,1), bBarrier ? FLinearColor(.5f,7.f,9.f) : Tint * .7f, .14f);
}

void AShadowPrototypeHUD::BeginPlay()
{
    Super::BeginPlay();
    Crosshair = Asset<UTexture2D>(TEXT("/Game/ShadowRun/UI/Mira_Padrao.Mira_Padrao"));
    HitMarker = Asset<UTexture2D>(TEXT("/Game/ShadowRun/UI/Mira_Confirmacao_Acerto.Mira_Confirmacao_Acerto"));
    ShopAmmoIcon = Asset<UTexture2D>(TEXT("/Game/ShadowRun/UI/Icone_Municao.Icone_Municao"));
    ShopHealthIcon = Asset<UTexture2D>(TEXT("/Game/ShadowRun/UI/Icone_Vida.Icone_Vida"));
    ShopShieldIcon = Asset<UTexture2D>(TEXT("/Game/ShadowRun/UI/Icone_Porta_Trancada.Icone_Porta_Trancada"));
    ShopReloadIcon = Asset<UTexture2D>(TEXT("/Game/ShadowRun/UI/Icone_Celula.Icone_Celula"));
    ButtonNormal = Asset<UTexture2D>(TEXT("/Game/ShadowRun/UI/Botao_Normal.Botao_Normal"));
    ButtonFocus = Asset<UTexture2D>(TEXT("/Game/ShadowRun/UI/Botao_Foco.Botao_Foco"));
    ButtonPressed = Asset<UTexture2D>(TEXT("/Game/ShadowRun/UI/Botao_Pressionado.Botao_Pressionado"));
    ButtonDisabled = Asset<UTexture2D>(TEXT("/Game/ShadowRun/UI/Botao_Desativado.Botao_Desativado"));
    PausePanel = Asset<UTexture2D>(TEXT("/Game/ShadowRun/UI/Pausa_Painel.Pausa_Painel"));
}

void AShadowPrototypeHUD::DrawHUD()
{
    Super::DrawHUD();
    auto* Player = Cast<AShadowRunner>(GetOwningPawn());
    const auto* Mode = Run(this);
    if (!Player || !Mode || !Canvas)
        return;

    const float W = Canvas->SizeX;
    const float H = Canvas->SizeY;
    if (Mode->GetMenu() != EShadowMenu::None)
    {
        DrawMenu(*Mode, *Player);
        return;
    }
    const auto EnsureFont = [this](TObjectPtr<UFont>& Typeface, const TCHAR* File) -> UFont* {
        if (!Typeface)
        {
            Typeface = NewObject<UFont>(this);
            Typeface->FontCacheType = EFontCacheType::Runtime;
            Typeface->GetMutableInternalCompositeFont().DefaultTypeface.AppendFont(NAME_None,
                FPaths::ProjectContentDir() / TEXT("ShadowRun/UI/Fonts") / File,
                EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
        }
        return Typeface.Get();
    };
    auto* Font = EnsureFont(MenuBodyFont, TEXT("ChakraPetch-SemiBold.ttf"));
    auto* TitleFont = EnsureFont(MenuTitleFont, TEXT("BarlowCondensed-ExtraBold.ttf"));
    if (!Font)
        Font = GEngine->GetMediumFont();
    if (!TitleFont)
        TitleFont = Font;
    const FLinearColor White(.91f,.95f,.97f);
    const FLinearColor Plate(.019f,.034f,.043f,.89f);
    const FLinearColor Muted(.63f,.72f,.77f);
    const float S = FMath::Clamp(FMath::Min(W/1920.f,H/1080.f),.86f,1.25f);
    const float Margin = 28.f*S;
    const auto Text = [this,Font](const FString& Label, FLinearColor Color, float X, float Y, float Scale=1.f) {
        DrawText(Label,Color,X,Y,Font,Scale);
    };
    const auto TitleText = [this,TitleFont](const FString& Label, FLinearColor Color, float X, float Y, float Scale=1.f) {
        DrawText(Label,Color,X,Y,TitleFont,Scale);
    };
    const auto Center = [this,Font,W](const FString& Label, FLinearColor Color, float Y, float Scale) {
        float Width, Height;
        GetTextSize(Label,Width,Height,Font,Scale);
        DrawText(Label,Color,(W-Width)/2,Y,Font,Scale);
    };

    if (!Mode->IsShopOpen())
    {
    const float Top = 24.f*S;
    const float HeaderW = 430.f*S;
    DrawRect(Plate,Margin,Top,HeaderW,92.f*S);
    DrawRect(Cyan,Margin,Top,4.f*S,92.f*S);
    TitleText(FString::Printf(TEXT("SHADOW RUN  /  ANDAR %02d"),Mode->FloorNumber),Cyan,Margin+18.f*S,Top+14.f*S,1.02f*S);
    const FString Goal = !Player->HasCarbine() ? TEXT("Pegue a carabina")
        : Mode->IsWorkshop() ? TEXT("Suba a escada")
        : Mode->Deposited == Mode->GetCellsNeeded() ? TEXT("Atravesse a comporta")
        : Mode->Deposited == Mode->GetCellsNeeded() ? TEXT("Passagem liberada")
        : Mode->Carried > 0 ? TEXT("Leve as células até o terminal")
        : TEXT("Aproxime-se e pressione E para coletar");
    Text(Goal,White,Margin+18.f*S,Top+52.f*S,1.16f*S);
    const float ObjectiveY = Top+74.f*S;
    Text(FString::Printf(TEXT("ENERGIA DA SALA   NECESSÁRIAS %d   |   CARREGADAS %d   |   DEPOSITADAS %d / %d"),
        Mode->GetCellsNeeded(),Mode->Carried,Mode->Deposited,Mode->GetCellsNeeded()),Cyan,Margin+18.f*S,ObjectiveY,.72f*S);

    const float ObjectiveW = 520.f*S;
    const float ObjectiveX = W-Margin-ObjectiveW;
    DrawRect(Plate,ObjectiveX,Top,ObjectiveW,112.f*S);
    DrawRect(Cyan,ObjectiveX,Top,4.f*S,112.f*S);
    TitleText(TEXT("OBJETIVO PRINCIPAL"),Cyan,ObjectiveX+18.f*S,Top+14.f*S,.92f*S);
    Text(Goal,White,ObjectiveX+18.f*S,Top+40.f*S,1.10f*S);
    Text(FString::Printf(TEXT("CÉLULAS  %d / %d      TERMINAL  %d      ABATES  %d"),
        Mode->Carried,Mode->GetCellsNeeded(),Mode->Deposited,Mode->Kills),White,ObjectiveX+18.f*S,Top+72.f*S,.86f*S);
    Text(FString::Printf(TEXT("%d CR   /   %d PTS"),Mode->GetCredits(),Mode->Score),Amber,ObjectiveX+18.f*S,Top+94.f*S,.82f*S);

    const float HealthX = Margin;
    const float HealthY = H-Margin-150.f*S;
    const float HealthW = 390.f*S;
    DrawRect(Plate,HealthX,HealthY,HealthW,132.f*S);
    DrawRect(Player->Health > Player->MaxHealth*.30f ? Cyan : FLinearColor::Red,HealthX,HealthY,5.f*S,132.f*S);
    TitleText(TEXT("INTEGRIDADE"),White,HealthX+20.f*S,HealthY+14.f*S,.92f*S);
    Text(FString::Printf(TEXT("VIDA   %d / %d"),Player->Health,Player->MaxHealth),White,HealthX+20.f*S,HealthY+39.f*S,1.08f*S);
    DrawRect(FLinearColor(.09f,.15f,.18f),HealthX+20.f*S,HealthY+58.f*S,HealthW-40.f*S,11.f*S);
    DrawRect(Player->Health>30 ? Cyan : FLinearColor::Red,HealthX+20.f*S,HealthY+58.f*S,(HealthW-40.f*S)*FMath::Clamp((float)Player->Health/FMath::Max(1,Player->MaxHealth),0.f,1.f),11.f*S);
    Text(FString::Printf(TEXT("ESCUDO   %d / %d"),Player->Shield,Player->MaxShield),Cyan,HealthX+20.f*S,HealthY+84.f*S,.92f*S);
    DrawRect(FLinearColor(.09f,.15f,.18f),HealthX+20.f*S,HealthY+101.f*S,HealthW-40.f*S,8.f*S);
    if (Player->MaxShield > 0)
        DrawRect(Cyan,HealthX+20.f*S,HealthY+101.f*S,(HealthW-40.f*S)*FMath::Clamp((float)Player->Shield/FMath::Max(1,Player->MaxShield),0.f,1.f),8.f*S);

    const float AmmoW = 360.f*S;
    const float AmmoX = W-Margin-AmmoW;
    const float AmmoY = H-Margin-106.f*S;
    DrawRect(Plate,AmmoX,AmmoY,AmmoW,88.f*S);
    const TCHAR* WeaponLabel = Player->HasPistolEquipped() ? TEXT("PISTOLA  /  2") : TEXT("CARABINA  /  1");
    const FLinearColor AmmoColor = Player->Magazine <= 3 ? FLinearColor(1.f,.35f,.18f) : Amber;
    Text(Player->IsReloading() ? TEXT("RECARREGANDO") : Player->Magazine == 0 ? TEXT("R  RECARREGAR") : WeaponLabel,
        AmmoColor,AmmoX+20.f*S,AmmoY+14.f*S,.94f*S);
    Text(FString::Printf(TEXT("%02d  /  %02d"),Player->Magazine,Player->Reserve),White,AmmoX+20.f*S,AmmoY+42.f*S,1.55f*S);
    Text(TEXT("CARREGADOR  /  RESERVA"),Muted,AmmoX+148.f*S,AmmoY+45.f*S,.67f*S);
    if (Player->IsReloading())
        DrawRect(Amber,AmmoX,AmmoY+82.f*S,AmmoW*Player->GetReloadProgress(),4.f*S);

    const bool bConfirmedHit = GetWorld()->GetTimeSeconds() < Mode->HitMarkerUntil;
    UTexture2D* Reticle = bConfirmedHit && HitMarker ? HitMarker.Get() : Crosshair.Get();
    const float ReticleSize = bConfirmedHit ? 38.f : 28.f;
    if (Reticle)
        DrawTextureSimple(Reticle, (W-ReticleSize)/2, (H-ReticleSize)/2, ReticleSize/FMath::Max(1,Reticle->GetSizeX()));
    else
    {
        DrawRect(White,W/2-8,H/2-1,16,2);
        DrawRect(White,W/2-1,H/2-8,2,16);
    }
    const bool bNearCell = Mode->HasNearbyCell(*Player);
    const bool bNearTerminal = FVector::Dist(Player->GetActorLocation(),Mode->TerminalPosition)<210 && Mode->Deposited<Mode->GetCellsNeeded();
    const bool bNearBench = Mode->HasWorkbench() &&
        FVector::DistSquared(Player->GetActorLocation(),Mode->GetWorkbenchPosition()) <= FMath::Square(210.f);
    if (bNearCell && !Mode->IsShopOpen() && !Mode->bFinished)
        Center(TEXT("[E]  COLETAR CÉLULA DE ENERGIA"),Cyan,H*.61f,1.16f*S);
    else if (bNearBench && !Mode->IsShopOpen() && !Mode->bFinished)
        Center(TEXT("[E]  BANCADA"),Cyan,H*.61f,1.10f*S);
    else if (bNearTerminal)
        Center(TEXT("[E]  DEPOSITAR CÉLULAS NO TERMINAL"),Cyan,H*.61f,1.10f*S);
    else if (!Mode->bFinished && !UGameplayStatics::IsGamePaused(this))
    {
        const FVector Target = Mode->GetObjectivePosition(Player->GetActorLocation());
        FVector2D Screen;
        auto* PC = GetOwningPlayerController();
        if (PC && PC->ProjectWorldLocationToScreen(Target, Screen) &&
            Screen.X > 58 && Screen.X < W-58 && Screen.Y > 106 && Screen.Y < H-103)
        {
            const int32 Meters = FMath::RoundToInt(FVector::Dist(Target,Player->GetActorLocation()) / 100);
            DrawRect(Plate,Screen.X-28*S,Screen.Y-12*S,56*S,25*S);
            DrawRect(Cyan,Screen.X-3*S,Screen.Y+14*S,6*S,3*S);
            Text(FString::Printf(TEXT("%d m"),Meters),Cyan,Screen.X-19*S,Screen.Y-9*S,.92f*S);
        }
    }
    if (GetWorld()->GetTimeSeconds()<Mode->FeedbackUntil)
        Center(Mode->Feedback,Cyan,H*.70f,1.18f*S);
    if (GetWorld()->GetTimeSeconds()<9)
        Center(TEXT("WASD mover  |  Esq. disparar  |  Dir. mirar  |  R recarregar  |  E interagir  |  1/2 armas  |  P pausa"),White,H-34.f*S,.86f*S);

    }
    if (Mode->IsShopOpen())
    {
        const bool bWorkshop = Mode->IsWorkshop();
        const float MenuS = FMath::Min(W/960.f, H/540.f);
        const float X = (W-872*MenuS)/2, Y = (H-410*MenuS)/2;
        const auto Box = [this,X,Y,MenuS](FLinearColor Color,float Left,float Top,float Width,float Height) {
            DrawRect(Color,X+Left*MenuS,Y+Top*MenuS,Width*MenuS,Height*MenuS);
        };
        const auto Label = [this,X,Y,MenuS](const FString& Value,FLinearColor Color,float Left,float Top,float Size,bool bTitle = false) {
            DrawMenuLabel(Value,Color,{X+Left*MenuS,Y+Top*MenuS},Size*MenuS,bTitle);
        };
        DrawRect(FLinearColor(.005f,.012f,.018f,.46f),0,0,W,H);
        Box(Cyan,26,25,4,44);
        Label(bWorkshop ? TEXT("OFICINA DE TRANSIÇÃO") : TEXT("OFICINA INICIAL"),White,43,17,34,true);
        Label(bWorkshop ? FString::Printf(TEXT("Andar %02d  >  %02d  /  Um perk por visita"),Mode->FloorNumber,Mode->FloorNumber+1)
                        : TEXT("Melhorias permanentes"),White,44,58,15);
        Box(FLinearColor(.012f,.018f,.023f,.94f),655,26,190,36);
        Label(FString::Printf(TEXT("CRÉDITOS  %d"),Mode->GetCredits()),Amber,667,32,18);

        const auto Cards = Mode->GetShopOptions(*Player);
        const TCHAR* Tracks[] = {TEXT("COMBATE"), TEXT("DEFESA"), TEXT("MANUSEIO"), TEXT("SUPRIMENTOS")};
        UTexture2D* Icons[] = {ShopAmmoIcon.Get(), ShopReloadIcon.Get(), ShopHealthIcon.Get(), ShopShieldIcon.Get()};
        UTexture2D* PerkIcons[] = {ShopAmmoIcon.Get(), ShopShieldIcon.Get(), ShopReloadIcon.Get(), ShopHealthIcon.Get()};
        for (int32 Index = 0; Index < Cards.Num(); ++Index)
        {
            const auto& Card = Cards[Index];
            const float Left = bWorkshop ? 26+Index*207 : 26+(Index%2)*416;
            const float Top = bWorkshop ? 99 : 99+(Index/2)*104;
            const float Width = bWorkshop ? 198 : 402;
            const float Height = bWorkshop ? 201 : 93;
            const FName CardName(TEXT("ShopCard"), Index + 1);
            const FVector2D At(X+Left*MenuS,Y+Top*MenuS), Size(Width*MenuS,Height*MenuS);
            const bool bHover = RegisterButton(CardName, At, Size);
            DrawButtonSurface(CardName,At,Size,Index == Mode->GetShopSelection() || bHover);
            Label(FString::FromInt(Index+1),Muted,Left+Width-29,Top+10,14);
            if (bWorkshop)
                Label(Tracks[Index],Cyan,Left+16,Top+11,15);
            const float IconX = Left+(bWorkshop ? (Width-52)/2 : 18);
            const float IconY = Top+(bWorkshop ? 43 : 19);
            if (auto* Icon = bWorkshop ? PerkIcons[Index] : Icons[Index])
                DrawTextureSimple(Icon,X+IconX*MenuS,Y+IconY*MenuS,52*MenuS/FMath::Max(1,Icon->GetSizeX()));
            const float TextX = Left+(bWorkshop ? 20 : 87);
            const float TextY = Top+(bWorkshop ? 107 : 12);
            Label(Card.Title,White,TextX,TextY,bWorkshop ? 21 : 23,true);
            Label(Card.Detail,Muted,TextX,TextY+25,bWorkshop ? 13 : 15);
            if (Card.Price)
                Label(FString::Printf(TEXT("%d CR"),Card.Price),Card.bAvailable ? Amber : Muted,TextX,TextY+50,15);
        }
        const auto& Selected = Cards[Mode->GetShopSelection()];
        const bool bBackHover = RegisterButton(TEXT("ShopBack"), {X+26*MenuS,Y+350*MenuS}, {290*MenuS,36*MenuS});
        const bool bBuyHover = RegisterButton(TEXT("ShopBuy"), {X+578*MenuS,Y+350*MenuS}, {267*MenuS,36*MenuS});
        DrawButtonSurface(TEXT("ShopBack"),{X+26*MenuS,Y+350*MenuS},{290*MenuS,36*MenuS},bBackHover);
        Label(bWorkshop ? TEXT("VOLTAR À PARTIDA  [E]") : TEXT("VOLTAR  [E]"),White,40,357,16);
        DrawButtonSurface(TEXT("ShopBuy"),{X+578*MenuS,Y+350*MenuS},{267*MenuS,36*MenuS},true,Selected.bAvailable);
        const bool bSelectedHeal = bWorkshop && Mode->GetShopSelection() == Cards.Num()-1;
        const FString BuyLabel = Selected.bAvailable ? FString::Printf(TEXT("COMPRAR   %d CR"),Selected.Price)
            : !Selected.Price ? TEXT("NÍVEL MÁXIMO")
            : bSelectedHeal ? (Mode->GetCredits() < Selected.Price && Player->Health < Player->MaxHealth
                && !Mode->HasBoughtHeal() ? TEXT("CRÉDITOS INSUFICIENTES") : Selected.Detail.ToUpper())
            : bWorkshop && Mode->HasBoughtPerk() ? TEXT("PERK JÁ ADQUIRIDO") : TEXT("CRÉDITOS INSUFICIENTES");
        Label(BuyLabel,Selected.bAvailable ? (bBuyHover ? White : Cyan) : Muted,592,356,18);
        if (Selected.bAvailable)
            Label(TEXT("[ENTER]"),Muted,779,359,13);
        if (GetWorld()->GetTimeSeconds()<Mode->FeedbackUntil)
            Label(Mode->Feedback,Amber,28,317,16);
    }
    if (Mode->bFinished)
    {
        DrawRect(FLinearColor(.005f,.01f,.018f,.84f),0,0,W,H);
        Center(Mode->bWon ? TEXT("EXTRAÇÃO CONCLUÍDA") : TEXT("TENTATIVA ENCERRADA"),Mode->bWon ? Cyan : Amber,H*.40f,2.0f);
        Center(TEXT("F5  Voltar à recepção"),White,H*.68f,1.1f);
        Center(FString::Printf(TEXT("Andar %d    Salas %d    Abates %d"),Mode->FloorNumber,Mode->RoomsCleared,Mode->Kills),White,H*.47f,1.f);
        Center(FString::Printf(TEXT("%d pontos    Recorde %d"),Mode->Score,Mode->GetBestScore()),Cyan,H*.52f,1.f);
        Center(FString::Printf(TEXT("Ganhos %d CR    Gastos %d CR    Saldo %d CR"),
            Mode->RunCreditsEarned,Mode->RunCreditsSpent,Mode->GetCredits()),Amber,H*.57f,1.f);
    }
}
