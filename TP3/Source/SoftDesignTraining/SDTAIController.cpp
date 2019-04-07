// Fill out your copyright notice in the Description page of Project Settings.

#include "SoftDesignTraining.h"
#include "SDTAIController.h"
#include "SDTCollectible.h"
#include "SDTFleeLocation.h"
#include "SDTPathFollowingComponent.h"
#include "DrawDebugHelpers.h"
#include "Kismet/KismetMathLibrary.h"
#include "UnrealMathUtility.h"
#include "SDTUtils.h"
#include "SoftDesignTrainingCharacter.h"
#include "EngineUtils.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"


ASDTAIController::ASDTAIController(const FObjectInitializer& ObjectInitializer)
    : m_isPlayerDetected(false)
	,m_isPlayerPoweredUp(false)
	,m_isTargetSeenBBKeyID(0)
	,m_isTargetPoweredUpBBKeyID(0)
	,m_fleePosBBKeyID(0)
	,m_targetPosBBKeyID(0)
	,Super(ObjectInitializer.SetDefaultSubobjectClass<USDTPathFollowingComponent>(TEXT("PathFollowingComponent")))
{
    m_PlayerInteractionBehavior = PlayerInteractionBehavior_Collect;
	m_behaviorTreeComponent = CreateDefaultSubobject<UBehaviorTreeComponent>(TEXT("BehaviorTreeComponent"));
	m_blackboardComponent = CreateDefaultSubobject<UBlackboardComponent>(TEXT("BlackboardComponent"));
	m_targetPlayerPos = FVector::ZeroVector;
	m_fleePos = FVector::ZeroVector;
	m_collectiblePos = FVector::ZeroVector;
}

void ASDTAIController::StartBehaviorTree(APawn* pawn) {
	if (ASoftDesignTrainingCharacter* character = Cast<ASoftDesignTrainingCharacter>(pawn))
	{
		if (character->GetBehaviorTree())
		{
			m_behaviorTreeComponent->StartTree(*character->GetBehaviorTree());
		}
	}
}

void ASDTAIController::StopBehaviorTree(APawn* pawn) {
	if (ASoftDesignTrainingCharacter* character = Cast<ASoftDesignTrainingCharacter>(pawn))
	{
		if (character->GetBehaviorTree())
		{
			m_behaviorTreeComponent->StopTree();
		}
	}
}


void ASDTAIController::GoToBestTarget(float deltaTime)
{
    switch (m_PlayerInteractionBehavior)
    {
    case PlayerInteractionBehavior_Collect:

        MoveToRandomCollectible();

        break;

    case PlayerInteractionBehavior_Chase:

        MoveToPlayer();

        break;

    case PlayerInteractionBehavior_Flee:

        MoveToBestFleeLocation();

        break;
    }
}

void ASDTAIController::MoveToRandomCollectible()
{
    float closestSqrCollectibleDistance = 18446744073709551610.f;
    ASDTCollectible* closestCollectible = nullptr;

    TArray<AActor*> foundCollectibles;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), ASDTCollectible::StaticClass(), foundCollectibles);

    while (foundCollectibles.Num() != 0)
    {
        int index = FMath::RandRange(0, foundCollectibles.Num() - 1);

        ASDTCollectible* collectibleActor = Cast<ASDTCollectible>(foundCollectibles[index]);
        if (!collectibleActor)
            return;

        if (!collectibleActor->IsOnCooldown())
        {
            MoveToLocation(foundCollectibles[index]->GetActorLocation(), 0.5f, false, true, true, NULL, false);
            OnMoveToTarget();
            return;
        }
        else
        {
            foundCollectibles.RemoveAt(index);
        }
    }
}

void ASDTAIController::MoveToPlayer()
{
    ACharacter * playerCharacter = UGameplayStatics::GetPlayerCharacter(GetWorld(), 0);
    if (!playerCharacter)
        return;

    MoveToActor(playerCharacter, 0.5f, false, true, true, NULL, false);
    OnMoveToTarget();
}

void ASDTAIController::PlayerInteractionLoSUpdate()
{
    ACharacter * playerCharacter = UGameplayStatics::GetPlayerCharacter(GetWorld(), 0);
    if (!playerCharacter)
        return;

    TArray<TEnumAsByte<EObjectTypeQuery>> TraceObjectTypes;
    TraceObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECollisionChannel::ECC_WorldStatic));
    TraceObjectTypes.Add(UEngineTypes::ConvertToObjectType(COLLISION_PLAYER));

    FHitResult losHit;
    GetWorld()->LineTraceSingleByObjectType(losHit, GetPawn()->GetActorLocation(), playerCharacter->GetActorLocation(), TraceObjectTypes);

    bool hasLosOnPlayer = false;

    if (losHit.GetComponent())
    {
        if (losHit.GetComponent()->GetCollisionObjectType() == COLLISION_PLAYER)
        {
            hasLosOnPlayer = true;
        }
    }

    if (hasLosOnPlayer)
    {
        if (GetWorld()->GetTimerManager().IsTimerActive(m_PlayerInteractionNoLosTimer))
        {
            GetWorld()->GetTimerManager().ClearTimer(m_PlayerInteractionNoLosTimer);
            m_PlayerInteractionNoLosTimer.Invalidate();
            DrawDebugString(GetWorld(), FVector(0.f, 0.f, 10.f), "Got LoS", GetPawn(), FColor::Red, 5.f, false);
        }
    }
    else
    {
        if (!GetWorld()->GetTimerManager().IsTimerActive(m_PlayerInteractionNoLosTimer))
        {
            GetWorld()->GetTimerManager().SetTimer(m_PlayerInteractionNoLosTimer, this, &ASDTAIController::OnPlayerInteractionNoLosDone, 3.f, false);
            DrawDebugString(GetWorld(), FVector(0.f, 0.f, 10.f), "Lost LoS", GetPawn(), FColor::Red, 5.f, false);
        }
    }
    
}

void ASDTAIController::OnPlayerInteractionNoLosDone()
{
    GetWorld()->GetTimerManager().ClearTimer(m_PlayerInteractionNoLosTimer);
    DrawDebugString(GetWorld(), FVector(0.f, 0.f, 10.f), "TIMER DONE", GetPawn(), FColor::Red, 5.f, false);

    if (!AtJumpSegment)
    {
        AIStateInterrupted();
        m_PlayerInteractionBehavior = PlayerInteractionBehavior_Collect;
    }
}

void ASDTAIController::MoveToBestFleeLocation()
{
    float bestLocationScore = 0.f;
    ASDTFleeLocation* bestFleeLocation = nullptr;

    ACharacter* playerCharacter = UGameplayStatics::GetPlayerCharacter(GetWorld(), 0);
    if (!playerCharacter)
        return;

    for (TActorIterator<ASDTFleeLocation> actorIterator(GetWorld(), ASDTFleeLocation::StaticClass()); actorIterator; ++actorIterator)
    {
        ASDTFleeLocation* fleeLocation = Cast<ASDTFleeLocation>(*actorIterator);
        if (fleeLocation)
        {
            float distToFleeLocation = FVector::Dist(fleeLocation->GetActorLocation(), playerCharacter->GetActorLocation());

            FVector selfToPlayer = playerCharacter->GetActorLocation() - GetPawn()->GetActorLocation();
            selfToPlayer.Normalize();

            FVector selfToFleeLocation = fleeLocation->GetActorLocation() - GetPawn()->GetActorLocation();
            selfToFleeLocation.Normalize();

            float fleeLocationToPlayerAngle = FMath::RadiansToDegrees(acosf(FVector::DotProduct(selfToPlayer, selfToFleeLocation)));
            float locationScore = distToFleeLocation + fleeLocationToPlayerAngle * 100.f;

            if (locationScore > bestLocationScore)
            {
                bestLocationScore = locationScore;
                bestFleeLocation = fleeLocation;
            }

            DrawDebugString(GetWorld(), FVector(0.f, 0.f, 10.f), FString::SanitizeFloat(locationScore), fleeLocation, FColor::Red, 5.f, false);
        }
    }

    if (bestFleeLocation)
    {
        MoveToLocation(bestFleeLocation->GetActorLocation(), 0.5f, false, true, false, NULL, false);
        OnMoveToTarget();
    }
}

void ASDTAIController::OnMoveToTarget()
{
    m_ReachedTarget = false;
}

void ASDTAIController::RotateTowards(const FVector& targetLocation)
{
    if (!targetLocation.IsZero())
    {
        FVector direction = targetLocation - GetPawn()->GetActorLocation();
        FRotator targetRotation = direction.Rotation();

        targetRotation.Yaw = FRotator::ClampAxis(targetRotation.Yaw);

        SetControlRotation(targetRotation);
    }
}

void ASDTAIController::SetActorLocation(const FVector& targetLocation)
{
    GetPawn()->SetActorLocation(targetLocation);
}

void ASDTAIController::OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
    Super::OnMoveCompleted(RequestID, Result);

    m_ReachedTarget = true;
}

void ASDTAIController::ShowNavigationPath()
{
    if (UPathFollowingComponent* pathFollowingComponent = GetPathFollowingComponent())
    {
        if (pathFollowingComponent->HasValidPath())
        {
            const FNavPathSharedPtr path = pathFollowingComponent->GetPath();
            TArray<FNavPathPoint> pathPoints = path->GetPathPoints();

            for (int i = 0; i < pathPoints.Num(); ++i)
            {
                DrawDebugSphere(GetWorld(), pathPoints[i].Location, 10.f, 8, FColor::Yellow);

                if (i != 0)
                {
                    DrawDebugLine(GetWorld(), pathPoints[i].Location, pathPoints[i - 1].Location, FColor::Yellow);
                }
            }
        }
    }
}

void ASDTAIController::UpdatePlayerInteraction(float deltaTime)
{
    //finish jump before updating AI state
    if (AtJumpSegment)
        return;

    APawn* selfPawn = GetPawn();
    if (!selfPawn)
        return;

    ACharacter* playerCharacter = UGameplayStatics::GetPlayerCharacter(GetWorld(), 0);
    if (!playerCharacter)
        return;

    FVector detectionStartLocation = selfPawn->GetActorLocation() + selfPawn->GetActorForwardVector() * m_DetectionCapsuleForwardStartingOffset;
    FVector detectionEndLocation = detectionStartLocation + selfPawn->GetActorForwardVector() * m_DetectionCapsuleHalfLength * 2;

    TArray<TEnumAsByte<EObjectTypeQuery>> detectionTraceObjectTypes;
    detectionTraceObjectTypes.Add(UEngineTypes::ConvertToObjectType(COLLISION_PLAYER));

    TArray<FHitResult> allDetectionHits;
    GetWorld()->SweepMultiByObjectType(allDetectionHits, detectionStartLocation, detectionEndLocation, FQuat::Identity, detectionTraceObjectTypes, FCollisionShape::MakeSphere(m_DetectionCapsuleRadius));

    FHitResult detectionHit;
    GetHightestPriorityDetectionHit(allDetectionHits, detectionHit);

    UpdatePlayerInteractionBehavior(detectionHit, deltaTime);

    if (GetMoveStatus() == EPathFollowingStatus::Idle)
    {
        m_ReachedTarget = true;
    }

    FString debugString = "";

    switch (m_PlayerInteractionBehavior)
    {
    case PlayerInteractionBehavior_Chase:
        debugString = "Chase";
        break;
    case PlayerInteractionBehavior_Flee:
        debugString = "Flee";
        break;
    case PlayerInteractionBehavior_Collect:
        debugString = "Collect";
        break;
    }

    DrawDebugString(GetWorld(), FVector(0.f, 0.f, 5.f), debugString, GetPawn(), FColor::Orange, 0.f, false);

    DrawDebugCapsule(GetWorld(), detectionStartLocation + m_DetectionCapsuleHalfLength * selfPawn->GetActorForwardVector(), m_DetectionCapsuleHalfLength, m_DetectionCapsuleRadius, selfPawn->GetActorQuat() * selfPawn->GetActorUpVector().ToOrientationQuat(), FColor::Blue);
}

bool ASDTAIController::HasLoSOnHit(const FHitResult& hit)
{
    if (!hit.GetComponent())
        return false;

    TArray<TEnumAsByte<EObjectTypeQuery>> TraceObjectTypes;
    TraceObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECollisionChannel::ECC_WorldStatic));

    FVector hitDirection = hit.ImpactPoint - hit.TraceStart;
    hitDirection.Normalize();

    FHitResult losHit;
    FCollisionQueryParams queryParams = FCollisionQueryParams();
    queryParams.AddIgnoredActor(hit.GetActor());

    GetWorld()->LineTraceSingleByObjectType(losHit, hit.TraceStart, hit.ImpactPoint + hitDirection, TraceObjectTypes, queryParams);

    return losHit.GetActor() == nullptr;
}

void ASDTAIController::AIStateInterrupted()
{
    StopMovement();
    m_ReachedTarget = true;
}

ASDTAIController::PlayerInteractionBehavior ASDTAIController::GetCurrentPlayerInteractionBehavior(const FHitResult& hit)
{
    if (m_PlayerInteractionBehavior == PlayerInteractionBehavior_Collect)
    {
        if (!hit.GetComponent())
            return PlayerInteractionBehavior_Collect;

        if (hit.GetComponent()->GetCollisionObjectType() != COLLISION_PLAYER)
            return PlayerInteractionBehavior_Collect;

        if (!HasLoSOnHit(hit))
            return PlayerInteractionBehavior_Collect;

        return SDTUtils::IsPlayerPoweredUp(GetWorld()) ? PlayerInteractionBehavior_Flee : PlayerInteractionBehavior_Chase;
    }
    else
    {
        PlayerInteractionLoSUpdate();

        return SDTUtils::IsPlayerPoweredUp(GetWorld()) ? PlayerInteractionBehavior_Flee : PlayerInteractionBehavior_Chase;
    }
}

void ASDTAIController::GetHightestPriorityDetectionHit(const TArray<FHitResult>& hits, FHitResult& outDetectionHit)
{
    for (const FHitResult& hit : hits)
    {
        if (UPrimitiveComponent* component = hit.GetComponent())
        {
            if (component->GetCollisionObjectType() == COLLISION_PLAYER)
            {
                //we can't get more important than the player
                outDetectionHit = hit;
                return;
            }
            else if(component->GetCollisionObjectType() == COLLISION_COLLECTIBLE)
            {
                outDetectionHit = hit;
            }
        }
    }
}

void ASDTAIController::UpdatePlayerInteractionBehavior(const FHitResult& detectionHit, float deltaTime)
{
    PlayerInteractionBehavior currentBehavior = GetCurrentPlayerInteractionBehavior(detectionHit);

    if (currentBehavior != m_PlayerInteractionBehavior)
    {
        m_PlayerInteractionBehavior = currentBehavior;
        AIStateInterrupted();
    }
}


/***********------- NEW CODE -------***********/


void ASDTAIController::DetectPlayer()
{
	//finish jump before updating AI state
	if (AtJumpSegment)
		return;

	APawn* selfPawn = GetPawn();
	if (!selfPawn)
		return;

	m_isPlayerDetected = false;
	AActor* targetPlayer = NULL;

	FVector detectionStartLocation = selfPawn->GetActorLocation() + selfPawn->GetActorForwardVector() * m_DetectionCapsuleForwardStartingOffset;
	FVector detectionEndLocation = detectionStartLocation + selfPawn->GetActorForwardVector() * m_DetectionCapsuleHalfLength * 2;

	TArray<TEnumAsByte<EObjectTypeQuery>> detectionTraceObjectTypes;
	detectionTraceObjectTypes.Add(UEngineTypes::ConvertToObjectType(COLLISION_PLAYER));

	TArray<FHitResult> allDetectionHits;
	GetWorld()->SweepMultiByObjectType(allDetectionHits, detectionStartLocation, detectionEndLocation, FQuat::Identity, detectionTraceObjectTypes, FCollisionShape::MakeSphere(m_DetectionCapsuleRadius));

	FHitResult detectionHit;
	GetHightestPriorityDetectionHit(allDetectionHits, detectionHit);

	// MAYBE TO REMOVE
	bool wasPlayerDetected = m_isPlayerDetected;

	if (detectionHit.GetComponent())
	{
		m_isPlayerDetected = detectionHit.GetComponent()->GetCollisionObjectType() == COLLISION_PLAYER;

		if (wasPlayerDetected != m_isPlayerDetected) {
			if(!AtJumpSegment)
				AIStateInterrupted();
		}

		if (m_isPlayerDetected) {
			ACharacter * playerCharacter = UGameplayStatics::GetPlayerCharacter(GetWorld(), 0);
			if (!playerCharacter)
				return;
			m_isPlayerPoweredUp = SDTUtils::IsPlayerPoweredUp(GetWorld());
			if (m_isPlayerPoweredUp) {
				FVector fleeLocation = GetBestFleeLocation();
				if (fleeLocation != FVector::ZeroVector)
					m_fleePos = fleeLocation;
			}
			else {
				m_targetPlayerPos = playerCharacter->GetActorLocation();
			}
		}
		else {
			FVector collectibleLocation = GetRandomCollectibleLocation();
			if (collectibleLocation != FVector::ZeroVector)
				m_collectiblePos = collectibleLocation;
		}
	}
	else {
		FVector collectibleLocation = GetRandomCollectibleLocation();
		if (collectibleLocation != FVector::ZeroVector)
			m_collectiblePos = collectibleLocation;
	}
		
}

void ASDTAIController::Possess(APawn* pawn)
{

	Super::Possess(pawn);

	if (ASoftDesignTrainingCharacter* character = Cast<ASoftDesignTrainingCharacter>(pawn))
	{
		if (character->GetBehaviorTree())
		{
			m_blackboardComponent->InitializeBlackboard(*character->GetBehaviorTree()->BlackboardAsset);

			m_targetPosBBKeyID = m_blackboardComponent->GetKeyID("TargetPos");
			m_isTargetSeenBBKeyID = m_blackboardComponent->GetKeyID("TargetIsSeen");
			m_isTargetPoweredUpBBKeyID = m_blackboardComponent->GetKeyID("IsPlayerPoweredUp");
			m_fleePosBBKeyID = m_blackboardComponent->GetKeyID("FleePos");
			m_collectiblePosBBKeyID = m_blackboardComponent->GetKeyID("CollectiblePos");

			//Set this agent in the BT
			m_blackboardComponent->SetValue<UBlackboardKeyType_Object>(m_blackboardComponent->GetKeyID("SelfActor"), pawn);
			m_blackboardComponent->SetValue<UBlackboardKeyType_Vector>(m_blackboardComponent->GetKeyID("TargetPos"), FVector::ZeroVector);
			m_blackboardComponent->SetValue<UBlackboardKeyType_Vector>(m_blackboardComponent->GetKeyID("FleePos"), FVector::ZeroVector);
			m_blackboardComponent->SetValue<UBlackboardKeyType_Vector>(m_blackboardComponent->GetKeyID("CollectiblePos"), FVector::ZeroVector);
			m_blackboardComponent->SetValue<UBlackboardKeyType_Bool>(m_blackboardComponent->GetKeyID("IsPlayerPoweredUp"), false);
			m_blackboardComponent->SetValue<UBlackboardKeyType_Bool>(m_blackboardComponent->GetKeyID("TargetIsSeen"), false);


		}
	}
}

FVector ASDTAIController::GetBestFleeLocation() {
	float bestLocationScore = 0.f;
	ASDTFleeLocation* bestFleeLocation = nullptr;

	ACharacter* playerCharacter = UGameplayStatics::GetPlayerCharacter(GetWorld(), 0);
	/*if (!playerCharacter)
		return;*/

	for (TActorIterator<ASDTFleeLocation> actorIterator(GetWorld(), ASDTFleeLocation::StaticClass()); actorIterator; ++actorIterator)
	{
		ASDTFleeLocation* fleeLocation = Cast<ASDTFleeLocation>(*actorIterator);
		if (fleeLocation)
		{
			float distToFleeLocation = FVector::Dist(fleeLocation->GetActorLocation(), playerCharacter->GetActorLocation());

			FVector selfToPlayer = playerCharacter->GetActorLocation() - GetPawn()->GetActorLocation();
			selfToPlayer.Normalize();

			FVector selfToFleeLocation = fleeLocation->GetActorLocation() - GetPawn()->GetActorLocation();
			selfToFleeLocation.Normalize();

			float fleeLocationToPlayerAngle = FMath::RadiansToDegrees(acosf(FVector::DotProduct(selfToPlayer, selfToFleeLocation)));
			float locationScore = distToFleeLocation + fleeLocationToPlayerAngle * 100.f;

			if (locationScore > bestLocationScore)
			{
				bestLocationScore = locationScore;
				bestFleeLocation = fleeLocation;
			}

			DrawDebugString(GetWorld(), FVector(0.f, 0.f, 10.f), FString::SanitizeFloat(locationScore), fleeLocation, FColor::Red, 5.f, false);
		}
	}

	FVector result = FVector::ZeroVector;
	if (bestFleeLocation) {
		result = bestFleeLocation->GetActorLocation();
	}

	return result;
}

FVector  ASDTAIController::GetRandomCollectibleLocation() {

	TArray<AActor*> foundCollectibles;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ASDTCollectible::StaticClass(), foundCollectibles);

	FVector CollectibleLocation = FVector::ZeroVector;

	
	while (foundCollectibles.Num() != 0)
	{
		int index = FMath::RandRange(0, foundCollectibles.Num() - 1);

		ASDTCollectible* collectibleActor = Cast<ASDTCollectible>(foundCollectibles[index]);
		/*if (!collectibleActor)
			return;*/
		if (collectibleActor) {
			if (!collectibleActor->IsOnCooldown())
			{
				CollectibleLocation = foundCollectibles[index]->GetActorLocation();
				return CollectibleLocation;
			}
			else
			{
				foundCollectibles.RemoveAt(index);
			}
		}
	}
	return CollectibleLocation;
}
