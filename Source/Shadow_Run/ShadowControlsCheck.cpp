#include "ShadowPrototype.h"

#if WITH_EDITOR
#include "Containers/Ticker.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraComponent.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Materials/MaterialInterface.h"
#include "UnrealClient.h"

namespace
{
    struct FControlsCheck
    {
        TWeakObjectPtr<AShadowPrototype> Mode;
        TWeakObjectPtr<UWorld> World;
        FVector Before;
        FRotator AimBefore;
        TArray<FVector> Cells;
        int32 Step = -4;
        int32 Failures = 0;
        double Due = 0;
        int32 BurstMagazine = 0;
        int32 Transitions = 0;
        TWeakObjectPtr<AActor> PreviousCell;
        TWeakObjectPtr<AActor> StagedDrone;
        TWeakObjectPtr<AActor> ShotBarrier;
        FVector StagedPosition;
        double DroneCheckDeadline = 0;
        int32 HealthBeforeDrone = 0;
        const FKey MoveKeys[4] = { EKeys::W, EKeys::S, EKeys::A, EKeys::D };
        const FVector MoveDirections[4] = { FVector(1,0,0), FVector(-1,0,0), FVector(0,-1,0), FVector(0,1,0) };

        void Verify(bool bPass, const TCHAR* Check)
        {
            Failures += bPass ? 0 : 1;
            UE_LOG(LogTemp, Display, TEXT("SR_CONTROLS %s %s"), bPass ? TEXT("PASS") : TEXT("FAIL"), Check);
        }

        bool Tick()
        {
            if (Step == 26 || Step == 27)
            {
                for (const FWorldContext& Context : GEngine->GetWorldContexts())
                    if (Context.WorldType == EWorldType::Game && Context.World())
                        World = Context.World();
            }
            auto* PC = World.IsValid() ? World->GetFirstPlayerController() : nullptr;
            auto* Runner = PC ? Cast<AShadowRunner>(PC->GetPawn()) : nullptr;
            if (!Runner || FPlatformTime::Seconds() < Due)
                return Step == 26 || Step == 27 || World.IsValid();

            const auto Input = [PC](const FKey& Key, EInputEvent Event, float Value) {
                PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, Event, Value));
            };
            const auto Tap = [&Input](const FKey& Key) {
                Input(Key, IE_Pressed, 1);
                Input(Key, IE_Released, 0);
            };
            const auto Wait = [this](double Seconds) {
                Due = FPlatformTime::Seconds() + Seconds;
            };
            const auto Capture = [](const TCHAR* Name) {
                if (FParse::Param(FCommandLine::Get(), TEXT("ShadowCaptureUI")))
                    FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/Name, false, false);
            };
            const auto VerifyWeaponFinish = [this,Runner](FName Expected) {
                TInlineComponentArray<UStaticMeshComponent*> Meshes(Runner);
                for (const auto* Mesh : Meshes)
                {
                    if (Mesh->GetFName() != TEXT("Carbine"))
                        continue;
                    const auto* Finish = Mesh->GetMaterial(0);
                    Verify(Finish && Finish->GetFName() == Expected, TEXT("Equipped weapon uses its gunmetal material"));
                    return;
                }
                Verify(false,TEXT("First-person weapon mesh exists"));
            };
            const auto ClickShop = [this, PC](FName Name, FVector2D Center)
            {
                auto* HUD = PC->GetHUD();
                if (!FParse::Param(FCommandLine::Get(), TEXT("ShadowCheckRenderedUI")))
                {
                    HUD->NotifyHitBoxClick(Name);
                    return;
                }
                FVector2D Size;
                World->GetGameViewport()->GetViewportSize(Size);
                const double Scale = FMath::Min(Size.X/960.0, Size.Y/540.0);
                const FVector2D Point = (Size - FVector2D(872,410)*Scale)/2 + Center*Scale;
                const auto* HitBox = HUD->GetHitBoxAtCoordinates(Point);
                Verify(HitBox && HitBox->GetName() == Name, TEXT("Rendered shop button matches viewport coordinates"));
                auto* ShadowHUD = CastChecked<AShadowPrototypeHUD>(HUD);
                ShadowHUD->UpdatePointer(Point, true);
                ShadowHUD->UpdatePointer(Point, false);
            };
            if (Step >= 0 && Step < 8)
            {
                const int32 Index = Step / 2;
                if (Step % 2 == 0)
                {
                    Runner->SetActorLocation(FVector(220,0,98));
                    Runner->GetCharacterMovement()->StopMovementImmediately();
                    Before = Runner->GetActorLocation();
                    Input(MoveKeys[Index], IE_Pressed, 1);
                    Wait(.29);
                }
                else
                {
                    UE_LOG(LogTemp, Display, TEXT("SR_DIAG %s delta=%s pos=%s mode=%d ignore=%d key=%f vel=%s block=%d collision=%d"), *MoveKeys[Index].ToString(),
                        *(Runner->GetActorLocation()-Before).ToString(), *Runner->GetActorLocation().ToString(),
                        int32(Runner->GetCharacterMovement()->MovementMode), PC->IsMoveInputIgnored(),
                        PC->PlayerInput->GetKeyValue(MoveKeys[Index]), *Runner->GetVelocity().ToString(),
                        Runner->GetCharacterMovement()->GetCurrentAcceleration().IsNearlyZero(),
                        int32(Runner->GetCapsuleComponent()->GetCollisionEnabled()));
                    Input(MoveKeys[Index], IE_Released, 0);
                    Verify(FVector::DotProduct(Runner->GetActorLocation()-Before, MoveDirections[Index]) > 30,
                           *MoveKeys[Index].ToString());
                    Wait(.12);
                }
                ++Step;
                return true;
            }

            switch (Step++)
            {
            case -4:
                Verify(Mode->GetMenu() == EShadowMenu::Main && UGameplayStatics::IsGamePaused(Runner) &&
                       PC->ShouldShowMouseCursor(), TEXT("Main menu opens paused with the mouse visible"));
                Capture(TEXT("Shadow_Run-Menu.png"));
                Wait(.18);
                break;
            case -3:
                Tap(EKeys::Down);
                Tap(EKeys::Enter);
                Wait(.12);
                break;
            case -2:
                Verify(Mode->GetMenu() == EShadowMenu::Settings, TEXT("Arrow and Enter open main menu settings"));
                Tap(EKeys::Escape);
                Wait(.12);
                break;
            case -1:
                Verify(Mode->GetMenu() == EShadowMenu::Main, TEXT("Escape returns to main menu"));
                PC->GetHUD()->NotifyHitBoxClick(FName(TEXT("MenuAction"), static_cast<int32>(EShadowMenuAction::Help) + 1));
                Verify(Mode->GetMenu() == EShadowMenu::Help, TEXT("Help button opens controls guide"));
                PC->GetHUD()->NotifyHitBoxClick(FName(TEXT("MenuAction"), static_cast<int32>(EShadowMenuAction::Back) + 1));
                Tap(EKeys::Enter);
                Step = -5;
                Wait(.12);
                break;
            case -5:
                Verify(Mode->GetMenu() == EShadowMenu::None && !UGameplayStatics::IsGamePaused(Runner) &&
                       !PC->ShouldShowMouseCursor(), TEXT("Play starts the attempt and captures game input"));
                Step = 0;
                break;
            case 8:
                Runner->SetActorLocation(FVector(345,0,98));
                Step = 104;
                Wait(.14);
                break;
            case 104:
                Verify(Runner->HasCarbine(), TEXT("Reception carbine pickup"));
                VerifyWeaponFinish(TEXT("M_CarbineGunmetal"));
                Runner->SetActorLocation(Mode->GetWorkbenchPosition()+FVector(-100,0,45));
                Tap(EKeys::E);
                Step = 117;
                Wait(.15);
                break;
            case 117:
                Verify(Mode->IsShopOpen() && PC->ShouldShowMouseCursor(), TEXT("E opens permanent upgrade bench with cursor"));
                Capture(TEXT("Shadow_Run-Loja.png"));
                Step = 125;
                Wait(.18);
                break;
            case 125:
                ClickShop(FName(TEXT("ShopCard"), 3), {227, 249});
                Verify(Mode->GetShopSelection()==2 && Mode->GetCredits()==200, TEXT("Clicking shop card selects without spending credits"));
                ClickShop(FName(TEXT("ShopCard"), 1), {227, 145});
                ClickShop(TEXT("ShopBuy"), {700, 368});
                Step = 118;
                Wait(.15);
                break;
            case 118:
                Verify(Mode->GetCredits()==120 && FMath::IsNearlyEqual(Runner->DamageMultiplier,1.05f),
                       TEXT("Permanent damage upgrade applies and debits 80 credits"));
                Tap(EKeys::One);
                Tap(EKeys::Enter);
                Wait(.15);
                break;
            case 119:
                Verify(Mode->GetCredits()==120 && Mode->RunCreditsSpent==80, TEXT("Insufficient funds reject next upgrade"));
                ClickShop(TEXT("ShopBack"), {170, 368});
                Verify(!Mode->IsShopOpen() && !PC->ShouldShowMouseCursor(), TEXT("Shop back button restores game input"));
                Wait(.15);
                break;
            case 120:
                Runner->SetActorLocation(FVector(220,0,98));
                Step = 9;
                Input(EKeys::SpaceBar, IE_Pressed, 1);
                Wait(.16);
                break;
            case 9:
                Verify(Runner->GetActorLocation().Z > 110, TEXT("Space jump"));
                Input(EKeys::SpaceBar, IE_Released, 0);
                AimBefore = PC->GetControlRotation();
                Input(EKeys::MouseX,IE_Axis,80);
                Input(EKeys::MouseY,IE_Axis,60);
                Wait(.12);
                break;
            case 10:
            {
                const auto Delta = (PC->GetControlRotation()-AimBefore).GetNormalized();
                Verify(FMath::Abs(Delta.Yaw) > .1, TEXT("Mouse horizontal"));
                Verify(FMath::Abs(Delta.Pitch) > .1, TEXT("Mouse vertical"));
                Input(EKeys::RightMouseButton, IE_Pressed, 1);
                Step = 101;
                Wait(.28);
                break;
            }
            case 101:
            {
                const auto* Camera = Runner->FindComponentByClass<UCameraComponent>();
                Verify(Camera && Camera->FieldOfView < 76.f, TEXT("Right click zooms aim"));
                Input(EKeys::RightMouseButton, IE_Released, 0);
                Input(EKeys::LeftMouseButton, IE_Pressed, 1);
                Wait(.43);
                break;
            }
            case 102:
                Input(EKeys::LeftMouseButton, IE_Released, 0);
                BurstMagazine = Runner->Magazine;
                Verify(BurstMagazine >= 19 && BurstMagazine <= 21, TEXT("Holding left click fires a burst at the configured cadence"));
                Wait(.25);
                break;
            case 103:
            {
                const auto* Camera = Runner->FindComponentByClass<UCameraComponent>();
                Verify(Camera && Camera->FieldOfView > 88.f, TEXT("Releasing right click restores field of view"));
                Verify(Runner->Magazine == BurstMagazine, TEXT("Releasing left click stops the burst"));
                Step = 11;
                break;
            }
            case 11:
                Verify(Runner->Magazine < 24 && Runner->Reserve == 96, TEXT("Left click fires and consumes ammo"));
                Tap(EKeys::R);
                Wait(.17);
                break;
            case 12:
                Verify(Runner->IsReloading(), TEXT("R starts reload"));
                Wait(1.4);
                break;
            case 13:
                Verify(!Runner->IsReloading() && Runner->Magazine == 24 && Runner->Reserve == 96 - (24 - BurstMagazine), TEXT("Reload fills magazine from reserve"));
                Tap(EKeys::Escape);
                Wait(.15);
                break;
            case 14:
                Verify(UGameplayStatics::IsGamePaused(Runner) && Mode->GetMenu() == EShadowMenu::Pause &&
                       PC->ShouldShowMouseCursor(), TEXT("Esc pauses and opens the pause menu"));
                Capture(TEXT("Shadow_Run-Pausa.png"));
                Step = 124;
                Wait(.18);
                break;
            case 124:
                PC->GetHUD()->NotifyHitBoxClick(FName(TEXT("MenuAction"), static_cast<int32>(EShadowMenuAction::Build) + 1));
                Verify(Mode->GetMenu() == EShadowMenu::Build && UGameplayStatics::IsGamePaused(Runner),
                       TEXT("Build screen opens while the game remains paused"));
                PC->GetHUD()->NotifyHitBoxClick(FName(TEXT("MenuAction"), static_cast<int32>(EShadowMenuAction::Back) + 1));
                Before = Runner->GetActorLocation();
                Input(EKeys::W,IE_Pressed,1);
                Tap(EKeys::LeftMouseButton);
                Step = 15;
                Wait(.25);
                break;
            case 15:
                Verify(Runner->GetActorLocation().Equals(Before,.1) && Runner->Magazine==24, TEXT("Pause freezes movement and fire"));
                Input(EKeys::W,IE_Released,0);
                Tap(EKeys::P);
                Wait(.15);
                break;
            case 16:
                Verify(!UGameplayStatics::IsGamePaused(Runner), TEXT("P resumes"));
                Tap(EKeys::Two);
                Step = 105;
                Wait(.15);
                break;
            case 105:
                Verify(Runner->HasPistolEquipped() && Runner->Magazine == 12 && Runner->Reserve == 48,
                       TEXT("2 equips pistol with separate ammo"));
                VerifyWeaponFinish(TEXT("M_PistolGunmetal"));
                PC->SetControlRotation(FRotator::ZeroRotator);
                Step = 131;
                Wait(.65);
                break;
            case 131:
                Capture(TEXT("Shadow_Run-Pistola.png"));
                Step = 128;
                Wait(.18);
                break;
            case 128:
                Input(EKeys::LeftMouseButton, IE_Pressed, 1);
                Step = 106;
                Wait(.40);
                break;
            case 106:
                Input(EKeys::LeftMouseButton, IE_Released, 0);
                Verify(Runner->Magazine == 11, TEXT("Pistol fires once per trigger press"));
                Tap(EKeys::R);
                Wait(1.2);
                break;
            case 107:
                Verify(Runner->Magazine == 12 && Runner->Reserve == 47, TEXT("Pistol reload uses its own reserve"));
                Tap(EKeys::One);
                Wait(.15);
                break;
            case 108:
                Verify(!Runner->HasPistolEquipped() && Runner->Magazine == 24 &&
                       Runner->Reserve == 96 - (24 - BurstMagazine), TEXT("1 restores carbine and its ammo"));
                Tap(EKeys::Two);
                Wait(.15);
                break;
            case 109:
                Verify(Runner->HasPistolEquipped() && Runner->Reserve == 47, TEXT("Pistol ammo persists across switching"));
                Tap(EKeys::One);
                Wait(.15);
                break;
            case 110:
            {
                AStaticMeshActor* Cell = nullptr;
                int32 Count = 0;
                for (TActorIterator<AStaticMeshActor> It(World.Get()); It; ++It)
                {
                    UStaticMesh* Mesh = It->GetStaticMeshComponent()->GetStaticMesh();
                    if (Mesh && Mesh->GetName() == TEXT("futuristic_energy_capsule_3d_model"))
                    {
                        Cell = *It;
                        ++Count;
                    }
                }
                Verify(Count == 1 && Cell, TEXT("Only the active room has a cell"));
                if (!Cell)
                {
                    FPlatformMisc::RequestExitWithStatus(false,1);
                    return false;
                }
                Verify(!Mode->IsPassageOpen(), TEXT("Door remains locked before deposit"));
                PreviousCell = Cell;
                PC->SetControlRotation(FRotator(-45.f,0.f,0.f));
                Runner->SetActorLocation(Cell->GetActorLocation()+FVector(0,0,47));
                Tap(EKeys::E);
                Step = 111;
                Wait(.15);
                break;
            }
            case 111:
                Verify(!PreviousCell.IsValid() && Mode->Carried == 1 && Mode->Deposited == 0, TEXT("One cell collected"));
                Verify(!StagedDrone.IsValid() || !StagedDrone->ActorHasTag(TEXT("ShadowStagedDrone")),
                       TEXT("Previous staged drone is active during the current room"));
                StagedDrone.Reset();
                ShotBarrier.Reset();
                Runner->SetActorLocation(Mode->TerminalPosition + FVector(-115,0,32));
                Tap(EKeys::E);
                Wait(1.2);
                break;
            case 112:
                Verify(Mode->Carried == 0 && Mode->Deposited == 1 && Mode->IsPassageOpen(),
                       TEXT("Terminal opens prepared passage"));
                for (TActorIterator<AActor> It(World.Get()); It; ++It)
                {
                    if (It->ActorHasTag(TEXT("ShadowStagedDrone")))
                        StagedDrone = *It;
                    if (It->ActorHasTag(TEXT("ShadowShotBarrier")))
                        ShotBarrier = *It;
                }
                Verify(StagedDrone.IsValid() == (Transitions < 3) && ShotBarrier.IsValid() == (Transitions < 3),
                       TEXT("Deposit stages drones and barrier only for upcoming combat rooms"));
                if (StagedDrone.IsValid() && ShotBarrier.IsValid())
                {
                    bool bFieldFits = false;
                    for (TActorIterator<AActor> It(World.Get()); It; ++It)
                    {
                        if (!It->ActorHasTag(TEXT("ShadowPortalField")))
                            continue;
                        FVector Center, Extent;
                        It->GetActorBounds(false,Center,Extent);
                        bFieldFits = FMath::IsNearlyEqual(Center.X,ShotBarrier->GetActorLocation().X,1.) &&
                            Extent.Y <= 145 && Extent.Z <= 105;
                        break;
                    }
                    Verify(bFieldFits, TEXT("Visible field fits inside the doorway blocker"));
                    StagedPosition = StagedDrone->GetActorLocation();
                    const FVector Eye = ShotBarrier->GetActorLocation() - FVector(130,0,28);
                    const FVector Aim = (StagedDrone->GetActorLocation() - Eye).GetSafeNormal();
                    FHitResult Hit;
                    FCollisionQueryParams Query;
                    Query.AddIgnoredActor(Runner);
                    World->LineTraceSingleByChannel(Hit, Eye, StagedDrone->GetActorLocation(), ECC_Visibility, Query);
                    const int32 PreviousKills = Mode->Kills;
                    for (int32 Shot = 0; Shot < 5; ++Shot)
                        Mode->Fire(*Runner, Eye, Aim, Eye);
                    Verify(Hit.GetActor() == ShotBarrier.Get() && StagedDrone.IsValid() && Mode->Kills == PreviousKills,
                           TEXT("Hologram blocks shots and staged drones survive"));
                }
                Tap(EKeys::E);
                if (Transitions == 0 && ShotBarrier.IsValid())
                {
                    Runner->SetActorLocation(ShotBarrier->GetActorLocation()-FVector(355,0,7));
                    PC->SetControlRotation(FRotator::ZeroRotator);
                    Step = 126;
                    Wait(.65);
                    break;
                }
                Wait(.14);
                break;
            case 126:
                Verify(StagedDrone.IsValid() && FVector::Dist(StagedPosition,StagedDrone->GetActorLocation()) > 20,
                       TEXT("Preloaded drone patrols behind the barrier"));
                Capture(TEXT("Shadow_Run-Barreira.png"));
                Step = 113;
                Wait(.2);
                break;
            case 113:
            {
                Verify(Mode->Deposited == 1, TEXT("Repeat interaction does not duplicate deposit"));
                const FVector Target = Mode->GetObjectivePosition(Runner->GetActorLocation());
                Runner->SetActorLocation(Target-FVector(340,0,54));
                Runner->GetCharacterMovement()->StopMovementImmediately();
                PC->SetControlRotation(FRotator::ZeroRotator);
                Runner->SetActorRotation(FRotator::ZeroRotator);
                Input(EKeys::W,IE_Pressed,1);
                Wait(.9);
                break;
            }
            case 114:
                Input(EKeys::W,IE_Released,0);
                ++Transitions;
                Verify(Mode->RoomsCleared == Transitions-1, TEXT("Crossing promotes next room once"));
                Verify(!ShotBarrier.IsValid() && (Transitions == 4 ||
                       (StagedDrone.IsValid() && !StagedDrone->ActorHasTag(TEXT("ShadowStagedDrone")))),
                       TEXT("Crossing removes the hologram and promotes the same drone"));
                Verify(Mode->IsWorkshop() == (Transitions==4), TEXT("Workshop follows three combat rooms"));
                if (Transitions == 1 && StagedDrone.IsValid())
                {
                    const FVector Position = StagedDrone->GetActorLocation();
                    FCollisionQueryParams Query;
                    Query.AddIgnoredActor(StagedDrone.Get());
                    const FCollisionObjectQueryParams StaticGeometry(ECC_WorldStatic);
                    Verify(!World->OverlapAnyTestByObjectType(Position,FQuat::Identity,StaticGeometry,
                           FCollisionShape::MakeSphere(70),Query), TEXT("Drone body has clearance from room geometry"));
                    Runner->SetActorLocation(FVector(Position.X,Position.Y+165,98));
                    Runner->GetCharacterMovement()->StopMovementImmediately();
                    HealthBeforeDrone = Runner->Health;
                    DroneCheckDeadline = FPlatformTime::Seconds()+4;
                    Step = 127;
                    break;
                }
                if (Transitions < 4)
                {
                    Step = 110;
                    break;
                }
                Verify(Mode->FloorNumber == 1, TEXT("Entering workshop does not raise floor"));
                Runner->SetActorLocation(Mode->GetWorkbenchPosition()+FVector(-100,0,45));
                Tap(EKeys::E);
                Step = 121;
                Wait(.15);
                break;
            case 127:
                if (Runner->Health == HealthBeforeDrone && FPlatformTime::Seconds() < DroneCheckDeadline)
                {
                    Step = 127;
                    break;
                }
                if (StagedDrone.IsValid())
                {
                    const FVector ToPlayer = (Runner->GetPawnViewLocation()-StagedDrone->GetActorLocation()).GetSafeNormal();
                    Verify(Runner->Health < HealthBeforeDrone &&
                           FVector::DotProduct(StagedDrone->GetActorForwardVector(),ToPlayer) > .97,
                           TEXT("Drone turns toward the player before firing"));
                }
                else
                    Verify(false,TEXT("Drone remains active during aim check"));
                Step = 110;
                break;
            case 121:
                Verify(Mode->IsShopOpen(), TEXT("E opens transition workshop"));
                Runner->Health = Runner->MaxHealth;
                Tap(EKeys::Four);
                Tap(EKeys::Enter);
                Step = 129;
                Wait(.15);
                break;
            case 129:
                Verify(Mode->GetCredits()==120 && !Mode->HasBoughtHeal(), TEXT("Full health rejects healing without charging"));
                Tap(EKeys::Two);
                Tap(EKeys::Enter);
                Step = 122;
                Wait(.15);
                break;
            case 122:
                Verify(Runner->Shield==25 && Runner->MaxShield==25 && Mode->GetCredits()==80,
                       TEXT("Defense perk adds shield and debits 40 credits"));
                Tap(EKeys::Three);
                Tap(EKeys::Enter);
                Wait(.15);
                break;
            case 123:
            {
                Verify(Mode->GetCredits()==80 && Mode->RunCreditsSpent==120, TEXT("Only one perk per workshop visit"));
                const int32 HealthBefore = Runner->Health;
                Runner->HitByDrone(15);
                Verify(Runner->Shield==10 && Runner->Health==HealthBefore, TEXT("Shield absorbs damage before health"));
                Runner->Health = Runner->MaxHealth-20;
                ClickShop(FName(TEXT("ShopCard"),4), {746,196});
                Capture(TEXT("Shadow_Run-Oficina-Cura.png"));
                Step = 130;
                Wait(.2);
                break;
            }
            case 130:
            {
                Verify(Mode->GetShopSelection()==3, TEXT("Healing card can be selected in the transition shop"));
                ClickShop(TEXT("ShopBuy"), {700,368});
                Verify(Runner->Health==Runner->MaxHealth && Mode->GetCredits()==55 &&
                       Mode->RunCreditsSpent==145 && Mode->HasBoughtHeal() && Mode->HasBoughtPerk(),
                       TEXT("Healing caps at maximum and is independent of the perk purchase"));
                Runner->Health -= 10;
                Mode->TryShopChoice(*Runner,3);
                Verify(Runner->Health==Runner->MaxHealth-10 && Mode->GetCredits()==55,
                       TEXT("Second heal in the same workshop is rejected"));
                Tap(EKeys::E);
                Step = 115;
                Wait(1.15);
                break;
            }
            case 115:
            {
                const FVector Target = Mode->GetObjectivePosition(Runner->GetActorLocation());
                Runner->SetActorLocation(Target-FVector(870,0,198));
                Runner->GetCharacterMovement()->StopMovementImmediately();
                PC->SetControlRotation(FRotator::ZeroRotator);
                Runner->SetActorRotation(FRotator::ZeroRotator);
                Input(EKeys::W,IE_Pressed,1);
                Wait(2.3);
                break;
            }
            case 116:
                Input(EKeys::W,IE_Released,0);
                Verify(Mode->FloorNumber == 2 && !Mode->IsWorkshop(), TEXT("Climbing stairs advances to floor two"));
                Runner->HitByDrone(999);
                Verify(Mode->bFinished && !Mode->bWon, TEXT("Death ends the run"));
                Verify(Mode->Score==300 && Mode->GetBestScore()==300, TEXT("Room score becomes record on defeat"));
                Tap(EKeys::F5);
                Step = 26;
                Wait(1.4);
                break;
            case 26:
            {
                auto* NewMode = World->GetAuthGameMode<AShadowPrototype>();
                Verify(NewMode && NewMode!=Mode.Get() && !NewMode->bFinished &&
                       NewMode->FloorNumber==1 && NewMode->RoomsCleared==0 && !Runner->HasCarbine() &&
                       NewMode->Carried==0 && NewMode->Deposited==0 && NewMode->Kills==0 &&
                       Runner->Health==100 && Runner->Magazine==24 && Runner->Reserve==96,
                       TEXT("F5 resets attempt and resources"));
                Verify(NewMode && NewMode->GetCredits()==55 && NewMode->GetBestScore()==300 &&
                       FMath::IsNearlyEqual(Runner->DamageMultiplier,1.05f) && Runner->MaxShield==0 &&
                       NewMode->Score==0 && NewMode->RunCreditsSpent==0,
                       TEXT("Reloaded SaveGame keeps permanent progress and clears run perks"));
                Verify(NewMode && NewMode->GetMenu() == EShadowMenu::None && !UGameplayStatics::IsGamePaused(Runner),
                       TEXT("F5 restarts directly in gameplay"));
                if (NewMode)
                {
                    Mode = NewMode;
                    NewMode->ToggleMenu();
                    PC->GetHUD()->NotifyHitBoxClick(FName(TEXT("MenuAction"), static_cast<int32>(EShadowMenuAction::Main) + 1));
                    Verify(NewMode->GetMenu() == EShadowMenu::ConfirmMain && UGameplayStatics::IsGamePaused(Runner),
                           TEXT("Returning to main asks before ending the attempt"));
                    PC->GetHUD()->NotifyHitBoxClick(FName(TEXT("MenuAction"), static_cast<int32>(EShadowMenuAction::LeaveRun) + 1));
                    Wait(1.4);
                    break;
                }
                [[fallthrough]];
            }
            case 27:
            {
                auto* NewMode = World->GetAuthGameMode<AShadowPrototype>();
                Verify(NewMode && NewMode != Mode.Get() && NewMode->GetMenu() == EShadowMenu::Main &&
                       UGameplayStatics::IsGamePaused(Runner) && NewMode->GetCredits()==55,
                       TEXT("Returning from pause opens main menu and preserves permanent progress"));
                UE_LOG(LogTemp, Display, TEXT("SR_CONTROLS_DONE failures=%d"), Failures);
                FPlatformMisc::RequestExitWithStatus(false,Failures ? 1 : 0);
                return false;
            }
            }
            return true;
        }
    };
}

void StartShadowControlsCheck(AShadowPrototype& Mode)
{
    static bool bStarted = false;
    if (bStarted || !FParse::Param(FCommandLine::Get(),TEXT("ShadowControlsCheck")))
        return;

    bStarted = true;
    auto Check = MakeShared<FControlsCheck>();
    Check->Mode = &Mode;
    Check->World = Mode.GetWorld();
    Check->Due = FPlatformTime::Seconds()+.7;
    FTSTicker::GetCoreTicker().AddTicker(TEXT("ShadowControlsCheck"),0,
        [Check](float) { return Check->Tick(); });
}
#endif
