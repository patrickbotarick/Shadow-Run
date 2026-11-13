#include "ShadowPrototype.h"

#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ShadowProfile.h"

namespace
{
    constexpr int32 MaxLevel = 5;
    constexpr int32 PerkCosts[] = { 50, 40, 40 };
    constexpr int32 HealIndex = 3;
    constexpr int32 HealPrice = 25;
    constexpr int32 HealAmount = 35;
    constexpr int32 UpgradeCosts[] = { 80, 65, 60, 75 };
    constexpr int32 MaxCredits = 999999;
}

void AShadowPrototype::LoadProfile()
{
    ProfileSlot = TEXT("ShadowRun_Profile");
    bool bTestProfile = false;
#if WITH_EDITOR
    if (FParse::Param(FCommandLine::Get(), TEXT("ShadowControlsCheck")))
    {
        static const FString TestSlot = TEXT("ShadowRun_Check_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        ProfileSlot = TestSlot;
        bTestProfile = true;
    }
#endif
    Profile = Cast<UShadowProfile>(UGameplayStatics::LoadGameFromSlot(ProfileSlot, 0));
    if (!Profile || Profile->Version != 1)
    {
        Profile = NewObject<UShadowProfile>(this);
        if (bTestProfile)
            Profile->Credits = 200;
    }

    Profile->Credits = FMath::Clamp(Profile->Credits, 0, MaxCredits);
    Profile->BestScore = FMath::Max(0, Profile->BestScore);
    for (int32 Index = 0; Index < 4; ++Index)
        UpgradeLevel(Index) = FMath::Clamp(UpgradeLevel(Index), 0, MaxLevel);
}

bool AShadowPrototype::SaveProfile()
{
    if (UGameplayStatics::SaveGameToSlot(Profile, ProfileSlot, 0))
        return true;
    UE_LOG(LogTemp, Warning, TEXT("Shadow Run: could not save profile %s"), *ProfileSlot);
    Notice(TEXT("Não foi possível salvar o perfil"));
    return false;
}

int32 AShadowPrototype::GetCredits() const { return Profile ? Profile->Credits : 0; }
int32 AShadowPrototype::GetBestScore() const { return Profile ? Profile->BestScore : 0; }

int32& AShadowPrototype::UpgradeLevel(int32 Index) const
{
    switch (Index)
    {
    case 0: return Profile->DamageLevel;
    case 1: return Profile->ReloadLevel;
    case 2: return Profile->HealthLevel;
    default: return Profile->ShieldLevel;
    }
}

bool AShadowPrototype::IsWorkbenchRoom() const
{
    return IsWorkshop() || Current.Kind == ERoomKind::Reception;
}

FVector AShadowPrototype::GetWorkbenchPosition() const
{
    return Current.Origin + (IsWorkshop() ? FVector(490, -240, 52) : FVector(405, -230, 52));
}

void AShadowPrototype::ApplyBuild(AShadowRunner& Player, bool bRefill) const
{
    const int32 PreviousHealth = Player.MaxHealth;
    const int32 PreviousShield = Player.MaxShield;
    Player.MaxHealth = 100 + Profile->HealthLevel * 10;
    Player.MaxShield = Profile->ShieldLevel * 20 + DefensePerks * 25;
    Player.DamageMultiplier = 1.f + Profile->DamageLevel * .05f + CombatPerks * .20f;
    Player.ReloadMultiplier = FMath::Max(.50f, 1.f - Profile->ReloadLevel * .06f - HandlingPerks * .15f);
    Player.Health = bRefill ? Player.MaxHealth : FMath::Min(Player.MaxHealth, Player.Health + Player.MaxHealth - PreviousHealth);
    Player.Shield = bRefill ? Player.MaxShield : FMath::Min(Player.MaxShield, Player.Shield + Player.MaxShield - PreviousShield);
}

bool AShadowPrototype::TryShopChoice(AShadowRunner& Player, int32 Index)
{
    if (!bShopOpen)
        return false;
    if (!IsWorkbenchRoom() || bFinished || FVector::DistSquared(Player.GetActorLocation(), GetWorkbenchPosition()) > FMath::Square(210.f))
    {
        CloseShop();
        return true;
    }

    const bool bTemporary = IsWorkshop();
    if (Index < 0 || Index >= 4)
        return true;
    if (bTemporary && Index == HealIndex)
    {
        if (Player.Health >= Player.MaxHealth || bHealBoughtThisVisit || Profile->Credits < HealPrice)
        {
            Notice(Player.Health >= Player.MaxHealth ? TEXT("Vida cheia") :
                bHealBoughtThisVisit ? TEXT("Cura já usada nesta visita") : TEXT("Créditos insuficientes"));
            return true;
        }
        Profile->Credits -= HealPrice;
        if (!SaveProfile())
        {
            Profile->Credits += HealPrice;
            return true;
        }
        const int32 Restored = FMath::Min(HealAmount, Player.MaxHealth - Player.Health);
        Player.Health += Restored;
        RunCreditsSpent += HealPrice;
        bHealBoughtThisVisit = true;
        Sound(TEXT("SFX_SR_UI_Confirmar"), .45f);
        Notice(*FString::Printf(TEXT("+%d de vida recuperada"), Restored));
        return true;
    }
    if (bTemporary && bPerkBoughtThisVisit)
    {
        Notice(TEXT("Um perk por visita"));
        return true;
    }
    if (!bTemporary && UpgradeLevel(Index) >= MaxLevel)
    {
        Notice(TEXT("Melhoria no nível máximo"));
        return true;
    }
    const int32 Price = bTemporary ? PerkCosts[Index] : UpgradeCosts[Index] * (UpgradeLevel(Index) + 1);
    if (Profile->Credits < Price)
    {
        Notice(TEXT("Créditos insuficientes"));
        return true;
    }

    Profile->Credits -= Price;
    if (!bTemporary)
        ++UpgradeLevel(Index);
    if (!SaveProfile())
    {
        Profile->Credits += Price;
        if (!bTemporary)
            --UpgradeLevel(Index);
        return true;
    }

    RunCreditsSpent += Price;
    if (bTemporary)
    {
        int32* Tracks[] = { &CombatPerks, &DefensePerks, &HandlingPerks };
        ++*Tracks[Index];
        bPerkBoughtThisVisit = true;
    }
    ApplyBuild(Player, false);
    Sound(TEXT("SFX_SR_UI_Confirmar"), .45f);
    Notice(bTemporary ? TEXT("Perk adquirido") : TEXT("Melhoria permanente adquirida"));
    return true;
}

bool AShadowPrototype::SelectShopCard(int32 Index)
{
    if (!bShopOpen)
        return false;
    if (Index >= 0 && Index < 4 && ShopSelection != Index)
    {
        ShopSelection = Index;
        Sound(TEXT("SFX_SR_UI_Foco"), .35f);
    }
    return true;
}

void AShadowPrototype::MoveShopSelection(int32 X, int32 Y)
{
    const int32 Index = IsWorkshop() ? (ShopSelection + X + Y + 4) % 4
        : (ShopSelection % 2 + X + 2) % 2 + 2 * ((ShopSelection / 2 + Y + 2) % 2);
    SelectShopCard(Index);
}

int32 AShadowPrototype::GetPerks(int32 Track) const
{
    const int32 Perks[] = {CombatPerks, DefensePerks, HandlingPerks};
    return Perks[Track];
}

TArray<FShadowShopCard> AShadowPrototype::GetShopOptions(const AShadowRunner& Player) const
{
    if (IsWorkshop())
        return {
            {TEXT("PULSO OFENSIVO"), TEXT("+20% de dano"), PerkCosts[0], !bPerkBoughtThisVisit && GetCredits() >= PerkCosts[0]},
            {TEXT("ESCUDO EXTRA"), TEXT("+25 de escudo"), PerkCosts[1], !bPerkBoughtThisVisit && GetCredits() >= PerkCosts[1]},
            {TEXT("RECARGA RÁPIDA"), TEXT("-15% no tempo de recarga"), PerkCosts[2], !bPerkBoughtThisVisit && GetCredits() >= PerkCosts[2]},
            {TEXT("KIT DE CURA"), bHealBoughtThisVisit ? TEXT("Usado nesta visita") :
                Player.Health >= Player.MaxHealth ? TEXT("Vida cheia") : FString::Printf(TEXT("Recupera %d de vida"),
                    FMath::Min(HealAmount, Player.MaxHealth - Player.Health)), HealPrice,
                !bHealBoughtThisVisit && Player.Health < Player.MaxHealth && GetCredits() >= HealPrice}
        };

    const FString Values[] = {
        FString::Printf(TEXT("%.0f%% > %.0f%%"), Player.DamageMultiplier * 100, Player.DamageMultiplier * 100 + 5),
        FString::Printf(TEXT("%.2f s > %.2f s"), 1.38f * Player.ReloadMultiplier, 1.38f * (Player.ReloadMultiplier - .06f)),
        FString::Printf(TEXT("%d > %d"), Player.MaxHealth, Player.MaxHealth + 10),
        FString::Printf(TEXT("%d > %d"), Player.MaxShield, Player.MaxShield + 20)
    };
    const TCHAR* Names[] = {TEXT("DANO"), TEXT("RECARGA"), TEXT("VIDA"), TEXT("ESCUDO")};
    TArray<FShadowShopCard> Lines;
    for (int32 Index = 0; Index < 4; ++Index)
    {
        const int32 Level = UpgradeLevel(Index);
        const int32 Price = UpgradeCosts[Index] * (Level + 1);
        Lines.Add({Names[Index], Level >= MaxLevel ? TEXT("NÍVEL MÁXIMO") : Values[Index],
                   Level >= MaxLevel ? 0 : Price, Level < MaxLevel && GetCredits() >= Price});
    }
    return Lines;
}

FString AShadowPrototype::GetBuildSummary(const AShadowRunner& Player) const
{
    return FString::Printf(TEXT("Dano %.0f%%   |   Recarga %.0f%%   |   Vida %d   |   Escudo %d\nCombate %d   /   Defesa %d   /   Manuseio %d"),
        Player.DamageMultiplier * 100, Player.ReloadMultiplier * 100, Player.MaxHealth, Player.MaxShield,
        CombatPerks, DefensePerks, HandlingPerks);
}
