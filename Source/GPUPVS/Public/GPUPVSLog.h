// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/**
 * GPUPVS 日志分类（按阶段输出）
 */
GPUPVS_API DECLARE_LOG_CATEGORY_EXTERN(LogGPUPVS, Log, All);

/**
 * 统一前缀：
 * - [Collect] / [Setup] / [Compute] / [Write]
 */
#define GPUPVS_LOG(Verbosity, Format, ...) UE_LOG(LogGPUPVS, Verbosity, TEXT("GPUPVS ") Format, ##__VA_ARGS__)
