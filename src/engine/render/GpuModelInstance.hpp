#pragma once

// Retired by T1. The per-draw block that used to live here is now
// GpuDrawInstance in VulkanRenderConstants.hpp: one struct describing one
// draw, shared by quads, models and the shadow pipelines, so that a draw's
// parameters can be read back from a storage buffer by instance index rather
// than pushed 256 bytes at a time.
//
// Nothing includes this any more. Safe to delete - it is only still here
// because this session cannot remove files from your working tree.
