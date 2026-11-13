#include "ShadowPrototype.h"

#include "AudioDevice.h"
#include "Camera/CameraActor.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/GameViewportClient.h"
#include "Fonts/CompositeFont.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Widgets/SViewport.h"

namespace
{
    const FLinearColor Cyan(.13f, .86f, .94f);
    const FLinearColor Amber(1.f, .65f, .23f);
    const FLinearColor White(.91f, .95f, .97f);
    const FLinearColor Muted(.63f, .72f, .77f);

    FName MenuButtonName(EShadowMenuAction Action)
    {
        return FName(TEXT("MenuAction"), static_cast<int32>(Action) + 1);
    }
}

bool AShadowPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
    if (!bMenuInput.Get(false))
        return Super::InputKey(Params);

    if (Params.Key == EKeys::LeftMouseButton)
    {
        FVector2D Point;
        if (auto* HUD = Cast<AShadowPrototypeHUD>(GetHUD()))
        {
            const bool bOverViewport = GetMousePosition(Point.X, Point.Y);
            HUD->UpdatePointer(bOverViewport ? Point : FVector2D(-1,-1),
                               Params.Event == IE_Pressed || Params.Event == IE_DoubleClick);
        }
        return true;
    }
    if (Params.Key.IsMouseButton() || Params.Key == EKeys::SpaceBar)
        return true;

    auto* Mode = GetWorld()->GetAuthGameMode<AShadowPrototype>();
    const int32 X = Params.Key == EKeys::Left ? -1 : Params.Key == EKeys::Right ? 1 : 0;
    const int32 Y = Params.Key == EKeys::Up ? -1 : Params.Key == EKeys::Down ? 1 : 0;
    if (Mode && (X || Y))
    {
        if (Params.Event == IE_Pressed || Params.Event == IE_Repeat)
        {
            if (Mode->IsShopOpen())
                Mode->MoveShopSelection(X,Y);
            else
                Mode->MoveMenuSelection(Y ? Y : X);
        }
        return true;
    }
    return Super::InputKey(Params);
}

void AShadowPlayerController::SetMenuInput(bool bEnabled)
{
    if (bMenuInput.IsSet() && bMenuInput.GetValue() == bEnabled)
        return;

    bMenuInput = bEnabled;
    SetShowMouseCursor(bEnabled);
    SetIgnoreMoveInput(bEnabled);
    SetIgnoreLookInput(bEnabled);
    if (bEnabled)
    {
        FInputModeGameAndUI InputMode;
        if (const auto* Viewport = GetWorld()->GetGameViewport())
            InputMode.SetWidgetToFocus(Viewport->GetGameViewportWidget());
        InputMode.SetHideCursorDuringCapture(false);
        InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        SetInputMode(InputMode);
    }
    else
    {
        FInputModeGameOnly InputMode;
        InputMode.SetConsumeCaptureMouseDown(false);
        SetInputMode(InputMode);
    }
}

void AShadowPrototype::CloseShop()
{
    if (bShopOpen)
    {
        bShopOpen = false;
        RefreshMenuInput();
    }
}

void AShadowPrototype::RefreshMenuInput()
{
    if (auto* PC = Cast<AShadowPlayerController>(GetWorld()->GetFirstPlayerController()))
    {
        const bool bEnabled = bShopOpen || Menu != EShadowMenu::None;
        if (bEnabled)
        {
            if (auto* Runner = Cast<AShadowRunner>(PC->GetPawn()))
                Runner->ReleaseCombatInput();
        }
        PC->SetMenuInput(bEnabled);
    }
}

void AShadowPrototype::SetMenu(EShadowMenu Page)
{
    Menu = Page;
    MenuSelection = 0;
    if (auto* PC = GetWorld()->GetFirstPlayerController())
    {
        if (Page == EShadowMenu::Main && !MainMenuCamera)
        {
            MainMenuCamera = GetWorld()->SpawnActor<ACameraActor>(FVector(760,-310,177),FRotator(-4,20,0));
            if (MainMenuCamera)
                PC->SetViewTarget(MainMenuCamera);
        }
        else if (Page == EShadowMenu::None && MainMenuCamera)
        {
            PC->SetViewTarget(PC->GetPawn());
            MainMenuCamera->Destroy();
            MainMenuCamera = nullptr;
        }
    }
    const bool bShouldPause = Page != EShadowMenu::None;
    if (UGameplayStatics::IsGamePaused(this) != bShouldPause)
        UGameplayStatics::SetGamePaused(this, bShouldPause);
    RefreshMenuInput();
}

void AShadowPrototype::ToggleMenu()
{
    if (bShopOpen)
    {
        CloseShop();
        return;
    }
    switch (Menu)
    {
    case EShadowMenu::None:
        Sound(TEXT("SFX_SR_UI_Pausar"));
        SetMenu(EShadowMenu::Pause);
        break;
    case EShadowMenu::Pause:
        ActivateMenuAction(EShadowMenuAction::Resume);
        break;
    case EShadowMenu::Settings:
    case EShadowMenu::Help:
    case EShadowMenu::Build:
    case EShadowMenu::ConfirmMain:
        SetMenu(ParentMenu);
        break;
    default:
        break;
    }
}

TArray<EShadowMenuAction> AShadowPrototype::GetMenuActions() const
{
    using A = EShadowMenuAction;
    switch (Menu)
    {
    case EShadowMenu::Main: return { A::Play, A::Settings, A::Help, A::Quit };
    case EShadowMenu::Pause: return { A::Resume, A::Build, A::Settings, A::Main };
    case EShadowMenu::Settings: return { A::SensitivityDown, A::SensitivityUp, A::VolumeDown, A::VolumeUp, A::Back };
    case EShadowMenu::Build:
    case EShadowMenu::Help: return { A::Back };
    case EShadowMenu::ConfirmMain: return { A::Back, A::LeaveRun };
    default: return {};
    }
}

void AShadowPrototype::MoveMenuSelection(int32 Direction)
{
    const int32 Count = GetMenuActions().Num();
    if (Count > 0)
    {
        MenuSelection = (MenuSelection + Direction + Count) % Count;
        Sound(TEXT("SFX_SR_UI_Foco"), .35f);
    }
}

void AShadowPrototype::ConfirmMenu()
{
    const auto Actions = GetMenuActions();
    if (Actions.IsValidIndex(MenuSelection))
        ActivateMenuAction(Actions[MenuSelection]);
}

void AShadowPrototype::ApplyVolume() const
{
    if (auto Device = GetWorld()->GetAudioDevice(); Device.IsValid())
        Device->SetTransientPrimaryVolume(Volume / 100.f);
}

void AShadowPrototype::ActivateMenuAction(EShadowMenuAction Action)
{
    using A = EShadowMenuAction;
    const auto Actions = GetMenuActions();
    const int32 Selected = Actions.IndexOfByKey(Action);
    if (Selected == INDEX_NONE)
        return;

    MenuSelection = Selected;
    switch (Action)
    {
    case A::Play:
    case A::Resume:
        SetMenu(EShadowMenu::None);
        Sound(TEXT("SFX_SR_UI_Continuar"));
        break;
    case A::Settings:
    case A::Help:
    case A::Build:
        ParentMenu = Menu;
        SetMenu(Action == A::Settings ? EShadowMenu::Settings : Action == A::Help ? EShadowMenu::Help : EShadowMenu::Build);
        break;
    case A::Back:
        SetMenu(ParentMenu);
        break;
    case A::Main:
        ParentMenu = Menu;
        SetMenu(EShadowMenu::ConfirmMain);
        break;
    case A::LeaveRun:
        UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this)));
        break;
    case A::Quit:
        UKismetSystemLibrary::QuitGame(this, GetWorld()->GetFirstPlayerController(), EQuitPreference::Quit, false);
        break;
    case A::SensitivityDown:
    case A::SensitivityUp:
    case A::VolumeDown:
    case A::VolumeUp:
    {
        const bool bSensitivity = Action == A::SensitivityDown || Action == A::SensitivityUp;
        int32& Value = bSensitivity ? Sensitivity : Volume;
        const int32 Delta = Action == A::SensitivityDown || Action == A::VolumeDown ? -10 : 10;
        Value = FMath::Clamp(Value + Delta, bSensitivity ? 20 : 0, bSensitivity ? 200 : 100);
        if (!bSensitivity)
            ApplyVolume();
        if (GConfig)
        {
            GConfig->SetInt(TEXT("ShadowRun.Controls"), bSensitivity ? TEXT("Sensitivity") : TEXT("Volume"), Value, GGameUserSettingsIni);
            GConfig->Flush(false, GGameUserSettingsIni);
        }
        break;
    }
    }
}

bool AShadowPrototypeHUD::RegisterButton(FName Name, FVector2D Position, FVector2D Size)
{
    AddHitBox(Position, Size, Name, true);
    FVector2D Point;
    const auto* PC = GetOwningPlayerController();
    return PC && PC->GetMousePosition(Point.X, Point.Y) && FBox2D(Position, Position + Size).IsInside(Point);
}

void AShadowPrototypeHUD::UpdatePointer(FVector2D Point, bool bPressed)
{
    const auto* HitBox = GetHitBoxAtCoordinates(Point);
    const FName Button = HitBox ? HitBox->GetName() : NAME_None;
    if (bPressed)
    {
        PressedButton = Button;
        return;
    }
    const bool bActivate = !Button.IsNone() && Button == PressedButton;
    PressedButton = NAME_None;
    if (bActivate)
        NotifyHitBoxClick(Button);
}

void AShadowPrototypeHUD::DrawMenuLabel(const FString& Label, FLinearColor Color, FVector2D At, float Size, bool bTitle)
{
    auto& Typeface = bTitle ? MenuTitleFont : MenuBodyFont;
    if (!Typeface)
    {
        const TCHAR* File = bTitle ? TEXT("BarlowCondensed-ExtraBold.ttf") : TEXT("ChakraPetch-SemiBold.ttf");
        Typeface = NewObject<UFont>(this);
        Typeface->FontCacheType = EFontCacheType::Runtime;
        Typeface->GetMutableInternalCompositeFont().DefaultTypeface.AppendFont(NAME_None,
            FPaths::ProjectContentDir() / TEXT("ShadowRun/UI/Fonts") / File, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
    }
    const FSlateFontInfo Font(Typeface.Get(), Size * .75f);
    FCanvasTextItem Item(At, FText::FromString(Label), Font, Color);
    Canvas->DrawItem(Item);
}

void AShadowPrototypeHUD::DrawButtonSurface(FName Name, FVector2D At, FVector2D Size, bool bHighlighted, bool bEnabled)
{
    auto* Texture = !bEnabled ? ButtonDisabled.Get() : !Name.IsNone() && PressedButton == Name ? ButtonPressed.Get()
        : bHighlighted ? ButtonFocus.Get() : ButtonNormal.Get();
    if (Texture)
        DrawTexture(Texture,At.X,At.Y,Size.X,Size.Y,0,0,1,1);
    else
        DrawRect(bHighlighted ? FLinearColor(.03f,.15f,.18f,.95f) : FLinearColor(.02f,.04f,.05f,.92f),
                 At.X,At.Y,Size.X,Size.Y);
}

void AShadowPrototypeHUD::NotifyHitBoxClick(FName BoxName)
{
    auto* Mode = GetWorld()->GetAuthGameMode<AShadowPrototype>();
    auto* Player = Cast<AShadowRunner>(GetOwningPawn());
    if (!Mode || !Player)
        return;

    if (Mode->IsShopOpen())
    {
        if (BoxName == TEXT("ShopBack"))
            Mode->CloseShop();
        else if (BoxName == TEXT("ShopBuy"))
            Mode->TryShopChoice(*Player, Mode->GetShopSelection());
        else if (BoxName.GetNumber() > 0 && BoxName == FName(TEXT("ShopCard"), BoxName.GetNumber()))
            Mode->SelectShopCard(BoxName.GetNumber() - 1);
        return;
    }

    for (const auto Action : Mode->GetMenuActions())
    {
        if (BoxName == MenuButtonName(Action))
        {
            Mode->ActivateMenuAction(Action);
            return;
        }
    }
}

void AShadowPrototypeHUD::DrawMenu(const AShadowPrototype& Mode, const AShadowRunner& Player)
{
    using A = EShadowMenuAction;
    const float W = Canvas->SizeX, H = Canvas->SizeY;
    const float S = FMath::Min(W/960.f, H/540.f);
    const auto Page = Mode.GetMenu();
    const bool bFront = Page == EShadowMenu::Main;
    const float X = bFront ? W*.065f : (W - 530*S)/2;
    const float Y = (H - 410*S)/2;
    const auto Box = [this,X,Y,S](FLinearColor Color,float Left,float Top,float Width,float Height) {
        DrawRect(Color, X+Left*S, Y+Top*S, Width*S, Height*S);
    };
    const auto Label = [this,X,Y,S](const FString& Value,FLinearColor Color,float Left,float Top,float Size, bool bTitle = false) {
        DrawMenuLabel(Value, Color, {X+Left*S,Y+Top*S}, Size*S, bTitle);
    };
    DrawRect(FLinearColor(.005f,.01f,.018f,bFront ? .36f : .42f),0,0,W,H);
    if (bFront)
        DrawRect(FLinearColor(.005f,.01f,.018f,.79f),0,0,X+325*S,H);
    else if (PausePanel)
        DrawTexture(PausePanel.Get(),X,Y,530*S,410*S,0,0,1,1);
    else
        Box(FLinearColor(.02f,.035f,.046f,.98f),0,0,530,410);

    const TCHAR* Title = TEXT("SHADOW RUN");
    const TCHAR* Subtitle = TEXT("SETOR INDUSTRIAL");
    switch (Mode.GetMenu())
    {
    case EShadowMenu::Pause:
        Title = TEXT("PAUSADO");
        Subtitle = TEXT("SHADOW RUN  /  SETOR INDUSTRIAL");
        break;
    case EShadowMenu::Settings: Title = TEXT("CONFIGURAÇÕES"); break;
    case EShadowMenu::Help: Title = TEXT("COMO JOGAR"); break;
    case EShadowMenu::Build: Title = TEXT("BUILD ATUAL"); break;
    case EShadowMenu::ConfirmMain: Title = TEXT("ENCERRAR TENTATIVA?");
    default: break;
    }
    Label(Subtitle, Cyan, 32, bFront ? 7 : 26, 13);
    if (bFront)
    {
        Label(TEXT("SHADOW"), White, 28, 18, 64, true);
        Label(TEXT("RUN"), White, 28, 77, 64, true);
    }
    else
        Label(Title, White, 30, 46, 36, true);

    const auto Button = [&](A Action, const FString& Caption, float Left, float Top, float Width, int32 Index) {
        const bool bHover = RegisterButton(MenuButtonName(Action), {X+Left*S,Y+Top*S}, {Width*S,45*S});
        const bool bFocused = bHover || Index == Mode.GetMenuSelection();
        DrawButtonSurface(MenuButtonName(Action),{X+Left*S,Y+Top*S},{Width*S,45*S},bFocused);
        Label(Caption.ToUpper(),bFocused ? Cyan : White,Left+16,Top+9,18);
    };
    if (Page == EShadowMenu::Settings)
    {
        Label(FString::Printf(TEXT("Sensibilidade do mouse   %d%%"), Mode.GetSensitivity()),White,32,146,17);
        Label(FString::Printf(TEXT("Volume geral   %d%%"), Mode.GetVolume()),White,32,215,17);
        Button(A::SensitivityDown,TEXT("-"),365,134,61,0);
        Button(A::SensitivityUp,TEXT("+"),439,134,61,1);
        Button(A::VolumeDown,TEXT("-"),365,203,61,2);
        Button(A::VolumeUp,TEXT("+"),439,203,61,3);
        Button(A::Back,TEXT("Voltar"),30,306,470,4);
    }
    else if (Page == EShadowMenu::Help || Page == EShadowMenu::Build || Page == EShadowMenu::ConfirmMain)
    {
        if (Page == EShadowMenu::Help)
        {
            Label(TEXT("Leve a célula ao terminal para abrir a próxima porta."),Amber,32,133,15);
            Label(TEXT("WASD  Mover   |   Espaço  Pular   |   E  Interagir"),White,32,174,16);
            Label(TEXT("Mouse  Mirar   |   Esq.  Atirar   |   Dir.  Aproximar"),White,32,204,16);
            Label(TEXT("R  Recarregar   |   1 / 2  Armas   |   Esc / P  Pausar"),White,32,234,16);
        }
        else if (Page == EShadowMenu::Build)
        {
            Label(FString::Printf(TEXT("ANDAR %02d    |    CRÉDITOS %d"), Mode.FloorNumber, Mode.GetCredits()),Amber,32,102,16);
            Box(FLinearColor(.01f,.025f,.03f,.9f),30,135,470,35);
            Label(FString::Printf(TEXT("VIDA %d   ESCUDO %d   DANO %.0f%%   RECARGA %.2f s"),
                  Player.MaxHealth,Player.MaxShield,Player.DamageMultiplier*100,Player.GetReloadDuration()),White,42,142,15);
            const TCHAR* Tracks[] = {TEXT("COMBATE"),TEXT("DEFESA"),TEXT("MANUSEIO")};
            const TCHAR* Perks[] = {TEXT("Pulso ofensivo"),TEXT("Escudo extra"),TEXT("Recarga rápida")};
            for (int32 Track = 0; Track < 3; ++Track)
            {
                const int32 Left = 30+Track*158;
                const int32 Count = Mode.GetPerks(Track);
                DrawButtonSurface(NAME_None,{X+Left*S,Y+185*S},{151*S,100*S},Count>0);
                Label(Tracks[Track], Count ? Cyan : White, Left+12,194,22,true);
                Label(Count ? FString::Printf(TEXT("%s  x%d"), Perks[Track],Count) : TEXT("Sem perks"),
                      Muted,Left+12,239,13);
            }
        }
        else
        {
            Label(TEXT("O progresso desta tentativa será encerrado."),White,32,151,16);
            Label(TEXT("Os créditos e as melhorias permanentes serão mantidos."),Muted,32,183,15);
            Button(A::LeaveRun,TEXT("Encerrar tentativa"),30,244,470,1);
        }
        Button(A::Back,Page == EShadowMenu::ConfirmMain ? TEXT("Cancelar") : TEXT("Voltar"),30,306,470,0);
    }
    else
    {
        const auto Actions = Mode.GetMenuActions();
        const TCHAR* MainLabels[] = {TEXT("Jogar"), TEXT("Configurações"), TEXT("Como jogar"), TEXT("Sair")};
        const TCHAR* PauseLabels[] = {TEXT("Continuar"), TEXT("Build atual"), TEXT("Configurações"), TEXT("Voltar ao menu principal")};
        for (int32 Index = 0; Index < Actions.Num(); ++Index)
            Button(Actions[Index], bFront ? MainLabels[Index] : PauseLabels[Index], 30, (bFront ? 158 : 120)+Index*49, bFront ? 264 : 470, Index);
    }
    Label(TEXT("SETAS  Navegar    |    ENTER  Selecionar"), Muted, 32, 373, 13);
}
