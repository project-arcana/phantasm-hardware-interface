#include "util.hh"

#include <phantasm-hardware-interface/common/byte_util.hh>
#include <phantasm-hardware-interface/common/format_size.hh>
#include <phantasm-hardware-interface/common/log.hh>

#include <clean-core/span.hh>

uint32_t phi::util::get_texture_size_bytes_on_gpu(arg::texture_description const& desc, bool is_d3d12, uint32_t max_num_mips)
{
    auto const numSlices = desc.get_array_size();
    auto const depth = desc.get_depth();

    auto const bytesPerPixel = util::get_format_size_bytes(desc.fmt);
    bool const bIsBlockFormat = util::is_block_compressed_format(desc.fmt);
    auto const bytesPerBlock = util::get_block_format_4x4_size(desc.fmt);

    uint32_t numBytesPerSlice = 0u;

    uint32_t const effective_num_mips = max_num_mips > 0 ? cc::min(max_num_mips, desc.num_mips) : desc.num_mips;
    for (uint32_t mip = 0; mip < effective_num_mips; ++mip)
    {
        uint32_t const rowLength = cc::max(1u, uint32_t(desc.width) >> mip);
        uint32_t const numDepths = cc::max(1u, uint32_t(depth) >> mip);
        uint32_t numRows = cc::max(1u, uint32_t(desc.height) >> mip);
        uint32_t pitch = cc::max(1u, rowLength * bytesPerPixel);

        if (bIsBlockFormat)
        {
            numRows = cc::max(1u, (numRows + 3) / 4);
            pitch = cc::max(bytesPerBlock, ((rowLength + 3) / 4) * bytesPerBlock);
        }

        uint32_t const numBytesOnDisk = numDepths * numRows * pitch;

        uint32_t numBytesOnGPU = numBytesOnDisk;
        if (is_d3d12)
        {
            // individual pixel / block rows must be 256 byte aligned in D3D12 GPU buffers
            // = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT
            numBytesOnGPU = numDepths * numRows * cc::align_up(pitch, 256);
        }

        // buffer offsets for subresources must be 512 byte aligned in D3D12
        // = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT
        // in Vulkan, there are multiple rules depending on texture content
        // (VUID-vkCmdCopyBufferToImage-bufferOffset-01558, VUID-vkCmdCopyBufferToImage-bufferOffset-01559, ...)
        // but 512 is a safe upper bound (larger than all 4x4 block sizes, etc.)
        numBytesOnGPU = cc::align_up(numBytesOnGPU, 512);

        numBytesPerSlice += numBytesOnGPU;
    }

    // NOTE: technically this is slightly larger than the real required amount because the last subresource would not have to be aligned up to 512
    return numBytesPerSlice * numSlices;
}

phi::util::texture_subresource_sizes phi::util::get_texture_subresource_sizes(phi::format fmt, uint32_t width, uint32_t height, uint32_t depth, uint32_t mip_idx)
{
    auto const bytesPerPixel = util::get_format_size_bytes(fmt);
    auto const bytesPerBlock = util::get_block_format_4x4_size(fmt);
    bool const bIsBlockFormat = util::is_block_compressed_format(fmt);

    uint32_t const rowLength = cc::max(1u, uint32_t(width) >> mip_idx);

    texture_subresource_sizes res = {};
    res.num_depths = cc::max(1u, uint32_t(depth) >> mip_idx);
    res.num_rows = cc::max(1u, uint32_t(height) >> mip_idx);

    if (bIsBlockFormat)
    {
        res.num_rows = cc::max(1u, (res.num_rows + 3) / 4);
        res.pitch_on_disk = cc::max(bytesPerBlock, ((rowLength + 3) / 4) * bytesPerBlock);
    }
    else
    {
        res.pitch_on_disk = cc::max(1u, rowLength * bytesPerPixel);
    }

    return res;
}

PHI_API uint32_t phi::util::get_texture_subresource_size_bytes_on_gpu(phi::format fmt, uint32_t width, uint32_t height, uint32_t depth, uint32_t mip_idx, bool is_d3d12)
{
    auto const subresSizes = get_texture_subresource_sizes(fmt, width, height, depth, mip_idx);
    return subresSizes.get_size_bytes_on_gpu(is_d3d12);
}

uint32_t phi::util::get_texture_pixel_byte_offset_on_gpu(tg::isize2 size, phi::format fmt, tg::ivec2 pixel, bool is_d3d12)
{
    CC_ASSERT(pixel.x < size.width && pixel.y < size.height && "pixel out of bounds");
    CC_ASSERT(!util::is_block_compressed_format(fmt) && "block compressed textures do not have 1:1 pixel mappings");

    auto const bytesPerPixel = get_format_size_bytes(fmt);

    uint32_t pitch = cc::max(1u, size.width * bytesPerPixel);

    if (is_d3d12)
    {
        pitch = phi::util::align_up(pitch, 256);
    }

    return pixel.y * pitch + pixel.x * bytesPerPixel;
}

bool phi::util::is_rowwise_texture_data_copy_in_bounds(uint32_t dest_row_stride_bytes, uint32_t row_size_bytes, uint32_t num_rows, uint32_t source_size_bytes, uint32_t destination_size_bytes)
{
    // num_rows is the height in pixels for regular formats, but is lower for block compressed formats
    auto const largest_src_access = num_rows * row_size_bytes;
    auto const largest_dest_access = (num_rows - 1) * dest_row_stride_bytes + row_size_bytes;

    bool const is_in_bounds = (largest_src_access <= source_size_bytes) && (largest_dest_access <= destination_size_bytes);

    if (!is_in_bounds)
    {
        PHI_LOG_WARN("rowwise copy from texture data to upload buffer is out of bounds");
        if (largest_src_access > source_size_bytes)
            PHI_LOG_WARN("src bound error: access {} > size {} (exceeding by {} B)", largest_src_access, source_size_bytes, largest_src_access - source_size_bytes);
        if (largest_dest_access > destination_size_bytes)
            PHI_LOG_WARN("dst bound error: access {} > size {} (exceeding by {} B)", largest_dest_access, destination_size_bytes,
                         largest_dest_access - destination_size_bytes);

        PHI_LOG_WARN("while writing {} rows of {} bytes (strided {})", num_rows, row_size_bytes, dest_row_stride_bytes);
    }

    return is_in_bounds;
}

uint32_t phi::util::copy_texture_data_rowwise(void const* __restrict srcArg, void* __restrict destArg, uint32_t dest_row_stride_bytes, uint32_t src_row_stride_bytes, uint32_t num_rows)
{
    CC_ASSERT(srcArg && destArg && src_row_stride_bytes > 0 && dest_row_stride_bytes > 0);
    CC_ASSUME(srcArg && destArg);

    std::byte const* src = static_cast<std::byte const*>(srcArg);
    std::byte* dest = static_cast<std::byte*>(destArg);

    auto const row_size_bytes = cc::min(dest_row_stride_bytes, src_row_stride_bytes);

    // num_rows is the height in pixels for regular formats, but is lower for block compressed formats
    for (auto y = 0u; y < num_rows; ++y)
    {
        auto const src_offset = y * src_row_stride_bytes;
        auto const dst_offset = y * dest_row_stride_bytes;

        // TODO: streaming memcpy (destination is not immediately accessed)
        std::memcpy(dest + dst_offset, src + src_offset, row_size_bytes);
    }

    return dest_row_stride_bytes * (num_rows - 1) + row_size_bytes;
}

void phi::util::unswizzle_bgra_texture_data(cc::span<std::byte> in_out_texture_data)
{
    for (auto i = 0u; i < in_out_texture_data.size(); i += 4)
    {
        cc::swap(in_out_texture_data[i], in_out_texture_data[i + 2]);
    }
}

void phi::util::shader_table_offsets::init(const phi::shader_table_strides& record_strides,
                                           uint32_t num_ray_gen_stacks,
                                           uint32_t num_miss_stacks,
                                           uint32_t num_hit_group_stacks,
                                           uint32_t num_callable_stacks)
{
    this->strides = record_strides;
    this->num_ray_gen_stacks = num_ray_gen_stacks;
    this->num_miss_stacks = num_miss_stacks;
    this->num_hit_group_stacks = num_hit_group_stacks;
    this->num_callable_stacks = num_callable_stacks;

    offset_ray_gen_base = 0;
    auto const ray_gen_full_stack_size = num_ray_gen_stacks * align_up(record_strides.size_ray_gen, 64);

    offset_miss_base = ray_gen_full_stack_size;
    auto const miss_full_stack_size = num_miss_stacks * align_up(record_strides.size_miss, 64);

    offset_hit_group_base = offset_miss_base + miss_full_stack_size;
    auto const hit_group_full_stack_size = num_hit_group_stacks * align_up(record_strides.size_hit_group, 64);

    offset_callable_base = offset_hit_group_base + hit_group_full_stack_size;
    auto const callable_full_stack_size = num_callable_stacks * align_up(record_strides.size_callable, 64);

    total_size = offset_callable_base + callable_full_stack_size;
}
