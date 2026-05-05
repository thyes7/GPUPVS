// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/**
 * GPUPVS 全局日志分类。
 *
 * 目标：
 * - 统一日志输出入口与前缀，避免 LogTemp 混杂导致难以筛选。
 * - 输出以阶段为中心（Collect / Setup / Compute / WriteBack），便于对齐 Lightmass 的流水线。
 */
DECLARE_LOG_CATEGORY_EXTERN(LogGPUPVS, Log, All);

/**
 * 统一日志前缀，按阶段输出：
 * - [Collect] 场景采集与扁平化
 * - [Setup]   栅格化与 cell 放置（对齐 PrecomputedVisibility）
 * - [Compute] 可见性计算（GPU/CPU）
 * - [Write]   回写资产或调试输出
 */
#define GPUPVS_LOG(Verbosity, Format, ...) UE_LOG(LogGPUPVS, Verbosity, TEXT("GPUPVS ") Format, ##__VA_ARGS__)
