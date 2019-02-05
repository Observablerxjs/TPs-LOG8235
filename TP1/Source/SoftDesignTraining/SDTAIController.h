// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"

#include "SDTAIController.generated.h"

/**
 * 
 */
UCLASS(ClassGroup = AI, config = Game)
class SOFTDESIGNTRAINING_API ASDTAIController : public AAIController
{
    GENERATED_BODY()
public:
    virtual void Tick(float deltaTime) override;
	float ComputeSpeed(float deltaTime);
	//virtual void MoveActor(float nSpeed);
	virtual void MoveForward(float speed, float deltaTime);
	virtual void Align();
	virtual void DetectObstable();
private:
	float const ACCELERATION = 5000.0f;
	float const MAX_SPEED = 50000000.0f;
};
