#pragma once

#include <cstdint>
#include <span>

namespace gamescope::output_staging
{

using FeatureFlags = uint32_t;

inline constexpr FeatureFlags FeatureSampled = 1u << 0;
inline constexpr FeatureFlags FeatureStorage = 1u << 1;
inline constexpr FeatureFlags FeatureTransferSrc = 1u << 2;
inline constexpr FeatureFlags FeatureTransferDst = 1u << 3;

// DRM_FORMAT_MOD_LINEAR and DRM_FORMAT_MOD_INVALID without a libdrm dependency.
inline constexpr uint64_t LinearModifier = 0;
inline constexpr uint64_t InvalidModifier = ~uint64_t{ 0 };

struct KmsModifier
{
	uint32_t format;
	uint64_t modifier;
};

struct VulkanModifier
{
	uint32_t format;
	uint64_t modifier;
	FeatureFlags features;
	bool combinedExportable;
	bool transferDstExportable;
};

enum class OutputMode
{
	Unsupported,
	Combined,
	Staged,
};

enum class Rejection
{
	Accepted,
	NoKmsFormat,
	NoOptimalComposition,
	NoKmsLinearModifier,
	NoExportableLinearTransferDst,
};

struct PlanInput
{
	uint32_t format;
	FeatureFlags optimalFeatures;
	std::span<const KmsModifier> kmsModifiers;
	std::span<const VulkanModifier> vulkanModifiers;
};

struct OutputPlan
{
	OutputMode mode = OutputMode::Unsupported;
	uint64_t modifier = InvalidModifier;
	Rejection rejection = Rejection::Accepted;
};

OutputPlan chooseOutputPlan( const PlanInput &input );

enum class ImageRole
{
	Unused,
	Client,
	CombinedOutput,
	OptimalComposition,
	LinearScanout,
	ExplicitOutput,
};

struct FrameActions
{
	ImageRole compositeTarget = ImageRole::Unused;
	ImageRole captureSource = ImageRole::Unused;
	ImageRole presentImage = ImageRole::Unused;
	uint32_t compositionDispatches = 0;
	uint32_t stagingCopies = 0;
};

FrameActions frameActions( OutputMode mode, bool directScanout, bool outputOverride );

uint32_t advanceRing( uint32_t current, bool submissionSucceeded, bool increment );
uint32_t lastSubmittedRing( uint32_t current, bool deferred );

enum class QueueOwner
{
	Undefined,
	Vulkan,
	Foreign,
};

struct CopyTransitions
{
	bool sourceShaderWriteToTransferRead;
	bool destinationOldContentsDiscarded;
	QueueOwner destinationBefore;
	QueueOwner destinationDuring;
	QueueOwner destinationAfter;
	bool transferWriteReleasedToForeign;
};

CopyTransitions stagedCopyTransitions( bool reusedScanoutImage );

} // namespace gamescope::output_staging
