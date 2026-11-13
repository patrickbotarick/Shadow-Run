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
    constexpr float PortalBarrierDepth = 80.f;
    constexpr float PortalFieldHeight = 204.f;
    constexpr float DroneRadius = 74.f;
    constexpr float DroneMeshYaw = 180.f;
    constexpr float WorkshopRise = 144.f;
    constexpr int32 CombatRoomsPerFloor = 3;
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
    if (!bPistol)
    {
        auto* Finish = Asset<UMaterialInterface>(TEXT("/Game/ShadowRun/Weapons/M_CarbineGunmetal.M_CarbineGunmetal"));
        if (Finish)
            Rifle->SetMaterial(0, Finish);
    }
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
    BuildRoom(Current);
    ActivateRoom();
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
    constexpr float PassageHeight = 210.f;
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
        Piece(FVector(RoomLength-29,JambY,PortalBase+PassageHeight/2), FVector(54,14,PassageHeight), Trim);
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
        const TArray<FVector> Cover = Room.Kind == ERoomKind::Cargo
            ? TArray<FVector>{{430,-220,58},{710,210,58},{1000,-90,58}}
            : TArray<FVector>{{435,0,76},{695,0,76},{940,160,58},{940,-245,58}};
        for (const FVector& At : Cover)
        {
            auto* Collider = Piece(At, FVector(142,135,At.Z*2), Trim);
            if (Crate)
            {
                Collider->SetActorHiddenInGame(true);
                Piece(At, FVector(150,143,At.Z*2+4), nullptr, Crate)->SetActorEnableCollision(false);
            }
        }
    }

    const TCHAR* Label = Room.Kind == ERoomKind::Reception ? TEXT("RECEPÇÃO")
        : Room.Kind == ERoomKind::Cargo ? TEXT("DEPÓSITO")
        : Room.Kind == ERoomKind::Machines ? TEXT("ANEL DE MÁQUINAS") : TEXT("OFICINA DE TRANSIÇÃO");
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
        : Room.Kind == ERoomKind::Cargo ? FLinearColor(1.f,.66f,.32f) : FLinearColor(.62f,.77f,1.f);
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
    const bool bCombat = Current.Kind == ERoomKind::Cargo || Current.Kind == ERoomKind::Machines;
    if (bCombat && RoomsThisFloor + 1 >= CombatRoomsPerFloor)
        Next.Kind = ERoomKind::Workshop;
    else if (bCombat)
        Next.Kind = Current.Kind == ERoomKind::Cargo ? ERoomKind::Machines : ERoomKind::Cargo;
    else
        Next.Kind = RoomRandom.RandRange(0,1) == 0 ? ERoomKind::Cargo : ERoomKind::Machines;

    const float ExitCandidates[] = {-220.f, 220.f};
    Next.ExitY = Next.Kind == ERoomKind::Workshop ? 0.f : ExitCandidates[RoomRandom.RandRange(0,1)];
    Next.Origin = Current.Origin + FVector(RoomLength,Current.ExitY,IsWorkshop() ? WorkshopRise : 0);
    const FBox CurrentBounds(Current.Origin+FVector(0,-520,0),Current.Origin+FVector(RoomLength,520,378));
    const FBox NextBounds(Next.Origin+FVector(1,-520,0),Next.Origin+FVector(RoomLength,520,378));
    bNextReady = !CurrentBounds.Intersect(NextBounds);
    if (bNextReady)
    {
        BuildRoom(Next);
        bNextReady = IsValid(Next.Door);
    }
    UE_LOG(LogTemp,Display,TEXT("SHADOW_ROOM_PREPARED kind=%d origin=%s ready=%d"),int32(Next.Kind),*Next.Origin.ToString(),bNextReady);
}

void AShadowPrototype::StageNextRoom()
{
    if (!bNextReady || bNextStaged)
        return;
    bNextStaged = true;
    if (Next.Kind != ERoomKind::Cargo && Next.Kind != ERoomKind::Machines)
        return;

    const FVector Candidates[] = {{560,-285,220},{790,325,220},{1130,-320,220},
                                  {1150,95,220},{465,320,220},{325,-335,220}};
    for (const FVector& Local : Candidates)
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

    BuildPortalBarrier(Next.Origin + FVector(PortalBarrierDepth,0,0));
}

void AShadowPrototype::BuildPortalBarrier(const FVector& Plane)
{
    auto* Blocker = Block(GetWorld(), Plane+FVector(0,0,180), FVector(4,RoomWidth+24,360));
    auto* Collision = Blocker->GetStaticMeshComponent();
    Collision->SetVisibility(false);
    Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
    Collision->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    Blocker->Tags.Add(TEXT("ShadowShotBarrier"));
    PortalShotBlocker = Blocker;
    PortalBarrier.Add(Blocker);

    PortalPlane = Plane;
    auto* Quad = Asset<UStaticMesh>(TEXT("/Game/ShadowRun/VFX/Malhas/SM_VFX_Quad.SM_VFX_Quad"));
    auto* FieldMaterial = Asset<UMaterialInterface>(TEXT("/Game/ShadowRun/VFX/M_PortalField.M_PortalField"));
    if (Quad && FieldMaterial)
    {
        auto* Field = Block(GetWorld(), Plane, FVector(PortalFieldHeight,2*PortalHalfWidth,1), Quad);
        Field->SetActorEnableCollision(false);
        Field->SetActorRotation(FRotator(90,0,0));
        Field->SetActorLocation(Plane+FVector(0,0,PortalFieldHeight/2) -
            Field->GetActorRotation().RotateVector(Quad->GetBoundingBox().GetCenter() * Field->GetActorScale3D()));
        Field->GetStaticMeshComponent()->SetCastShadow(false);
        PortalField = Field->GetStaticMeshComponent()->CreateDynamicMaterialInstance(0, FieldMaterial);
        PortalBarrier.Add(Field);
    }
    auto* Trim = Asset<UMaterialInterface>(TEXT("/Game/ShadowRun/Sector/M_SectorTrim.M_SectorTrim"));
    auto* CyanSurface = Asset<UMaterialInterface>(TEXT("/Game/ShadowRun/Sector/M_SectorCyan.M_SectorCyan"));
    for (float Side : {-1.f, 1.f})
    {
        const double Y = Side*(PortalHalfWidth+6);
        auto* Emitter = Block(GetWorld(), Plane+FVector(0,Y,102), FVector(26,17,204));
        Emitter->SetActorEnableCollision(false);
        Emitter->GetStaticMeshComponent()->SetMaterial(0, Trim);
        PortalBarrier.Add(Emitter);
        auto* Core = Block(GetWorld(), Plane+FVector(-14,Y,102), FVector(4,5,174));
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
    Sign->SetActorLocationAndRotation(Plane+FVector(-12,0,220), FRotator(0,180,0));
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
    const FVector Positions[] = {{565,-300,49},{850,275,49},{1160,-225,49}};
    const FVector CellAt = bReception ? FVector(640,0,49) : Positions[RoomRandom.RandRange(0,2)];
    auto* Cell = Block(GetWorld(),Current.Origin+CellAt,FVector(45,45,87),Model(TEXT("futuristic_energy_capsule_3d_model")));
    Cell->SetActorEnableCollision(false);
    Current.Geometry.Add(Lamp(GetWorld(),Current.Origin+CellAt+FVector(0,0,20),Cyan,1150,180));
    Pickups.Add({Cell,true});
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
        const int32 AmmoLimit = Player->HasPistolEquipped() ? PistolReserveLimit : ReserveLimit;
        if (!Pickup.bCell && Player->Reserve >= AmmoLimit)
            continue;
        if (Pickup.bCell)
        {
            ++Carried;
            Sound(TEXT("SFX_SR_Celula_Coletada"));
            Notice(TEXT("Célula coletada"));
        }
        else
        {
            Player->Reserve = FMath::Min(AmmoLimit, Player->Reserve + (Player->HasPistolEquipped() ? 12 : 24));
            Sound(TEXT("SFX_SR_Municao_Coletada"));
            Notice(TEXT("Munição coletada"));
        }
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
    if (IsPassageOpen() && Local.X >= PortalBarrierDepth && Local.X < 350 &&
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
    const bool bBarrierHit = PortalShotBlocker && Hit.GetActor() == PortalShotBlocker;
    ShotEffects(Muzzle, Hit.bBlockingHit ? Hit.ImpactPoint : AimPoint, false, Hit.bBlockingHit, bBarrierHit);
    static const TCHAR* ShotSounds[] = {TEXT("SFX_SR_Tiro_Carabina_01"), TEXT("SFX_SR_Tiro_Carabina_02"),
                                       TEXT("SFX_SR_Tiro_Carabina_03")};
    Sound(ShotSounds[FMath::RandRange(0, 2)], Player.HasPistolEquipped() ? .34f : .48f);
    if (bBarrierHit)
    {
        if (PortalField)
        {
            const FVector At = Hit.ImpactPoint - PortalPlane;
            PortalField->SetVectorParameterValue(TEXT("Impact"), FLinearColor(
                FMath::Clamp(.5 + At.Y / (2*PortalHalfWidth),0.,1.),
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
    auto* Font = GEngine->GetMediumFont();
    const FLinearColor White(.91f,.95f,.97f);
    const FLinearColor Plate(.019f,.034f,.043f,.89f);
    const auto Text = [this,Font](const FString& Label, FLinearColor Color, float X, float Y, float Scale=1.f) {
        DrawText(Label,Color,X,Y,Font,Scale);
    };
    const auto Center = [this,Font,W](const FString& Label, FLinearColor Color, float Y, float Scale) {
        float Width, Height;
        GetTextSize(Label,Width,Height,Font,Scale);
        DrawText(Label,Color,(W-Width)/2,Y,Font,Scale);
    };

    if (!Mode->IsShopOpen())
    {
    DrawRect(Plate,24,25,315,69);
    DrawRect(Cyan,24,25,3,69);
    Text(FString::Printf(TEXT("SHADOW RUN  /  ANDAR %02d"),Mode->FloorNumber),Cyan,37,34,.88f);
    const FString Goal = !Player->HasCarbine() ? TEXT("Pegue a carabina")
        : Mode->IsWorkshop() ? TEXT("Suba a escada")
        : Mode->Deposited == Mode->GetCellsNeeded() ? TEXT("Atravesse a comporta")
        : Mode->Carried + Mode->Deposited == Mode->GetCellsNeeded() ? TEXT("Leve a célula ao terminal")
        : TEXT("Encontre a célula de energia");
    Text(Goal,White,37,58,1.15f);
    DrawRect(Plate,W-279,25,255,92);
    Text(FString::Printf(TEXT("CÉLULAS   %d / %d"),Mode->Carried+Mode->Deposited,Mode->GetCellsNeeded()),Cyan,W-264,35,1.07f);
    Text(FString::Printf(TEXT("NO TERMINAL  %d     ABATES  %d"),Mode->Deposited,Mode->Kills),White,W-264,63,.85f);
    Text(FString::Printf(TEXT("%d CR   /   %d PTS"),Mode->GetCredits(),Mode->Score),Amber,W-264,86,.9f);

    DrawRect(Plate,24,H-108,228,83);
    Text(FString::Printf(TEXT("VIDA  %d / %d"),Player->Health,Player->MaxHealth),White,37,H-103,1.05f);
    DrawRect(FLinearColor(.12f,.19f,.22f),37,H-79,202,4);
    DrawRect(Player->Health>30 ? Cyan : FLinearColor::Red,37,H-79,202.f*Player->Health/Player->MaxHealth,4);
    Text(FString::Printf(TEXT("ESCUDO  %d / %d"),Player->Shield,Player->MaxShield),Cyan,37,H-72,.9f);
    if (Player->MaxShield > 0)
        DrawRect(Cyan,37,H-47,202.f*Player->Shield/Player->MaxShield,4);
    DrawRect(Plate,W-247,H-82,223,57);
    const TCHAR* WeaponLabel = Player->HasPistolEquipped() ? TEXT("PISTOLA  /  2") : TEXT("CARABINA  /  1");
    Text(Player->IsReloading() ? TEXT("RECARREGANDO") : Player->Magazine == 0 ? TEXT("R  RECARREGAR") : WeaponLabel,
        Amber,W-234,H-78,.90f);
    Text(FString::Printf(TEXT("%02d  /  %02d"),Player->Magazine,Player->Reserve),White,W-234,H-56,1.3f);
    if (Player->IsReloading())
        DrawRect(Amber, W-247, H-28, 223 * Player->GetReloadProgress(), 3);

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
    const bool bNearTerminal = FVector::Dist(Player->GetActorLocation(),Mode->TerminalPosition)<210 && Mode->Deposited<Mode->GetCellsNeeded();
    const bool bNearBench = Mode->HasWorkbench() &&
        FVector::DistSquared(Player->GetActorLocation(),Mode->GetWorkbenchPosition()) <= FMath::Square(210.f);
    if (bNearBench && !Mode->IsShopOpen() && !Mode->bFinished)
        Center(TEXT("[E]  BANCADA"),Cyan,H*.61f,1.05f);
    else if (bNearTerminal)
        Center(TEXT("[E]  TERMINAL DE ENERGIA"),Cyan,H*.61f,1.05f);
    else if (!Mode->bFinished && !UGameplayStatics::IsGamePaused(this))
    {
        const FVector Target = Mode->GetObjectivePosition(Player->GetActorLocation());
        FVector2D Screen;
        auto* PC = GetOwningPlayerController();
        if (PC && PC->ProjectWorldLocationToScreen(Target, Screen) &&
            Screen.X > 58 && Screen.X < W-58 && Screen.Y > 106 && Screen.Y < H-103)
        {
            const int32 Meters = FMath::RoundToInt(FVector::Dist(Target,Player->GetActorLocation()) / 100);
            DrawRect(Plate,Screen.X-24,Screen.Y-10,48,22);
            DrawRect(Cyan,Screen.X-3,Screen.Y+12,6,3);
            Text(FString::Printf(TEXT("%d m"),Meters),Cyan,Screen.X-16,Screen.Y-8,.92f);
        }
    }
    if (GetWorld()->GetTimeSeconds()<Mode->FeedbackUntil)
        Center(Mode->Feedback,Cyan,H*.70f,1.1f);
    if (GetWorld()->GetTimeSeconds()<9)
        Center(TEXT("WASD mover  |  Esq. disparar  |  Dir. mirar  |  R recarregar  |  E interagir  |  1/2 armas  |  P pausa"),White,H-31,.85f);

    }
    if (Mode->IsShopOpen())
    {
        const bool bWorkshop = Mode->IsWorkshop();
        const float S = FMath::Min(W/960.f, H/540.f);
        const float X = (W-872*S)/2, Y = (H-410*S)/2;
        const FLinearColor Muted(.63f,.72f,.77f);
        const auto Box = [this,X,Y,S](FLinearColor Color,float Left,float Top,float Width,float Height) {
            DrawRect(Color,X+Left*S,Y+Top*S,Width*S,Height*S);
        };
        const auto Label = [this,X,Y,S](const FString& Value,FLinearColor Color,float Left,float Top,float Size,bool bTitle = false) {
            DrawMenuLabel(Value,Color,{X+Left*S,Y+Top*S},Size*S,bTitle);
        };
        DrawRect(FLinearColor(.005f,.012f,.018f,.46f),0,0,W,H);
        Box(Cyan,26,25,4,44);
        Label(bWorkshop ? TEXT("OFICINA DE TRANSIÇÃO") : TEXT("OFICINA INICIAL"),White,43,17,34,true);
        Label(bWorkshop ? FString::Printf(TEXT("Andar %02d  >  %02d  /  Um perk por visita"),Mode->FloorNumber,Mode->FloorNumber+1)
                        : TEXT("Melhorias permanentes"),White,44,58,15);
        Box(FLinearColor(.012f,.018f,.023f,.94f),655,26,190,36);
        Label(FString::Printf(TEXT("CRÉDITOS  %d"),Mode->GetCredits()),Amber,667,32,18);

        const auto Cards = Mode->GetShopOptions(*Player);
        const TCHAR* Tracks[] = {TEXT("COMBATE"), TEXT("DEFESA"), TEXT("MANUSEIO")};
        UTexture2D* Icons[] = {ShopAmmoIcon.Get(), ShopReloadIcon.Get(), ShopHealthIcon.Get(), ShopShieldIcon.Get()};
        UTexture2D* PerkIcons[] = {ShopAmmoIcon.Get(), ShopShieldIcon.Get(), ShopReloadIcon.Get()};
        for (int32 Index = 0; Index < Cards.Num(); ++Index)
        {
            const auto& Card = Cards[Index];
            const float Left = bWorkshop ? 26+Index*277 : 26+(Index%2)*416;
            const float Top = bWorkshop ? 99 : 99+(Index/2)*104;
            const float Width = bWorkshop ? 264 : 402;
            const float Height = bWorkshop ? 201 : 93;
            const FName CardName(TEXT("ShopCard"), Index + 1);
            const FVector2D At(X+Left*S,Y+Top*S), Size(Width*S,Height*S);
            const bool bHover = RegisterButton(CardName, At, Size);
            DrawButtonSurface(CardName,At,Size,Index == Mode->GetShopSelection() || bHover);
            Label(FString::FromInt(Index+1),Muted,Left+Width-29,Top+10,14);
            if (bWorkshop)
                Label(Tracks[Index],Cyan,Left+16,Top+11,15);
            const float IconX = Left+(bWorkshop ? 104 : 18);
            const float IconY = Top+(bWorkshop ? 43 : 19);
            if (auto* Icon = bWorkshop ? PerkIcons[Index] : Icons[Index])
                DrawTextureSimple(Icon,X+IconX*S,Y+IconY*S,52*S/FMath::Max(1,Icon->GetSizeX()));
            const float TextX = Left+(bWorkshop ? 20 : 87);
            const float TextY = Top+(bWorkshop ? 107 : 12);
            Label(Card.Title,White,TextX,TextY,23,true);
            Label(Card.Detail,Muted,TextX,TextY+25,15);
            if (Card.Price)
                Label(FString::Printf(TEXT("%d CR"),Card.Price),Card.bAvailable ? Amber : Muted,TextX,TextY+50,15);
        }
        const auto& Selected = Cards[Mode->GetShopSelection()];
        const bool bBackHover = RegisterButton(TEXT("ShopBack"), {X+26*S,Y+350*S}, {290*S,36*S});
        const bool bBuyHover = RegisterButton(TEXT("ShopBuy"), {X+578*S,Y+350*S}, {267*S,36*S});
        DrawButtonSurface(TEXT("ShopBack"),{X+26*S,Y+350*S},{290*S,36*S},bBackHover);
        Label(bWorkshop ? TEXT("VOLTAR À PARTIDA  [E]") : TEXT("VOLTAR  [E]"),White,40,357,16);
        DrawButtonSurface(TEXT("ShopBuy"),{X+578*S,Y+350*S},{267*S,36*S},true,Selected.bAvailable);
        const FString BuyLabel = Selected.bAvailable ? FString::Printf(TEXT("COMPRAR   %d CR"),Selected.Price)
            : !Selected.Price ? TEXT("NÍVEL MÁXIMO") : Mode->IsWorkshop() && Mode->HasBoughtPerk()
            ? TEXT("PERK JÁ ADQUIRIDO") : TEXT("CRÉDITOS INSUFICIENTES");
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
