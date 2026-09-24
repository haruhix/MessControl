#include "MCTaskActor.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/GameplayStatics.h"

AMCTaskActor::AMCTaskActor()
{
    PrimaryActorTick.bCanEverTick = true; bReplicates = true; bAlwaysRelevant = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TaskVisual")); Visual->SetupAttachment(RootComponent);
    Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("ProgressLabel")); Label->SetupAttachment(RootComponent);
    Label->SetRelativeLocation(FVector(0,0,145)); Label->SetRelativeRotation(FRotator(0,180,0));
    Label->SetHorizontalAlignment(EHTA_Center); Label->SetWorldSize(23); Label->SetTextRenderColor(FColor(255,240,194));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Coffee(TEXT("/Game/Art/Meshes/SM_Coffee"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Food(TEXT("/Game/Art/Meshes/SM_Food"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Tooth(TEXT("/Game/Art/Meshes/SM_ToothProp"));
    static ConstructorHelpers::FObjectFinder<UMCSoundPalette> Sound(TEXT("/Game/Data/DA_MouthSounds"));
    CoffeeMesh = Coffee.Object; FoodMesh = Food.Object; ToothMesh = Tooth.Object; SoundPalette = Sound.Object;
}
void AMCTaskActor::BeginPlay() { Super::BeginPlay(); OnRep_Visuals(); }
void AMCTaskActor::OnConstruction(const FTransform& Transform) { Super::OnConstruction(Transform); OnRep_Visuals(); }
void AMCTaskActor::Initialize(UMCDayEvent* Event) { Kind = Event->Kind; WorkSeconds = FMath::Max(0.2f,Event->WorkSeconds); }
void AMCTaskActor::OnRep_Visuals()
{
    Visual->SetStaticMesh(Kind == EMCTaskKind::Coffee ? CoffeeMesh : Kind == EMCTaskKind::Food ? FoodMesh : ToothMesh);
    const TCHAR* Hint = Kind == EMCTaskKind::Coffee ? TEXT("LMB / BRUSH") : Kind == EMCTaskKind::Food ? TEXT("E / PULL") : TEXT("E / REPAIR");
    Label->SetText(FText::FromString(Progress >= 1.f ? TEXT("SPARKLING!") : FString::Printf(TEXT("%s  %d%%"),Hint,FMath::RoundToInt(Progress*100))));
}
void AMCTaskActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const float Time = GetWorld()->GetTimeSeconds();
    if (APawn* LocalPawn = UGameplayStatics::GetPlayerPawn(this,0))
        Label->SetVisibility(FVector::DistSquared2D(LocalPawn->GetActorLocation(),GetActorLocation()) < FMath::Square(400.f));
    if (Progress >= 1.f) { Visual->SetRelativeScale3D(FVector(FMath::Max(0.01f,Visual->GetRelativeScale3D().X-DeltaSeconds*3))); return; }
    if (Kind == EMCTaskKind::Coffee) Visual->SetRelativeScale3D(FVector(FMath::Max(0.12f,1.f-Progress*0.85f)));
    if (Kind == EMCTaskKind::Food) { Visual->SetRelativeLocation(FVector(0,0,Progress*75)); Visual->SetRelativeRotation(FRotator(0,0,FMath::Sin(Time*8)*Progress*8)); }
    if (Kind == EMCTaskKind::LooseTooth) Visual->SetRelativeRotation(FRotator(0,0,FMath::Sin(Time*4)*18*(1-Progress)));
}
bool AMCTaskActor::ApplyWork(AMCToothCharacter* Worker, bool bBrush, float DeltaSeconds)
{
    if (!HasAuthority() || bResolved || !IsValid(Worker) || Worker->GetWorld()!=GetWorld()) return false;
    AMCGameState* State = GetWorld()->GetGameState<AMCGameState>();
    if (!State || State->Phase != EMCShiftPhase::Working || bBrush != (Kind == EMCTaskKind::Coffee)) return false;
    if (FVector::DistSquared2D(Worker->GetActorLocation(),GetActorLocation()) > FMath::Square(180.f) || FMath::Abs(Worker->GetActorLocation().Z-GetActorLocation().Z)>140) return false;
    Progress = FMath::Clamp(Progress + FMath::Clamp(DeltaSeconds,0.f,0.2f)/WorkSeconds,0.f,1.f);
    OnRep_Visuals(); ForceNetUpdate();
    if (Progress >= 1.f)
    {
        bResolved = true; MulticastComplete(); SetLifeSpan(0.65f);
        if (AMCGameMode* Mode = GetWorld()->GetAuthGameMode<AMCGameMode>()) Mode->ResolveTask(this);
    }
    return true;
}
void AMCTaskActor::MulticastComplete_Implementation() { if (SoundPalette) SoundPalette->Play(this,TEXT("Complete"),GetActorLocation()); }
void AMCTaskActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AMCTaskActor,Kind); DOREPLIFETIME(AMCTaskActor,Progress);
}
