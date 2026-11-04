#include "ptfx_validation.h"
#include <cmath>
#include <cstring>

bool ptfx::validateHeader(const PtfxHeader& h, size_t fileSize, uint32_t codeCapacity, uint32_t scratchCapacity)
{
    if (h.magic != PTFX_MAGIC || h.format_version != 1 || h.num_params > 12 ||
        (h.flags & ~uint16_t(0x1f)) || !h.code_size || h.code_size > codeCapacity ||
        h.bss_size > codeCapacity - h.code_size || h.scratch_size > scratchCapacity ||
        h.code_offset < sizeof(PtfxHeader) || uint64_t(h.code_offset) + h.code_size > fileSize ||
        (h.init_offset & ~1u) >= h.code_size || (h.set_param_offset & ~1u) >= h.code_size ||
        (h.process_block_offset & ~1u) >= h.code_size ||
        !memchr(h.display_name, 0, sizeof(h.display_name)) || !memchr(h.author, 0, sizeof(h.author))) return false;
    for (unsigned i = 0; i < h.num_params; ++i)
    {
        const auto& p = h.params[i];
        if (!memchr(p.label, 0, sizeof(p.label)) || !std::isfinite(p.min_val) ||
            !std::isfinite(p.max_val) || !std::isfinite(p.default_val) || !std::isfinite(p.step) ||
            p.min_val > p.max_val || p.default_val < p.min_val || p.default_val > p.max_val || p.step <= 0) return false;
        if (p.display_type == PARAM_LIST)
        {
            unsigned offset = uint8_t(p.unit[0]) | unsigned(uint8_t(p.unit[1])) << 8;
            unsigned count = uint8_t(p.unit[2]);
            if (!count || offset >= h.code_size || p.min_val != 0 || p.max_val != count - 1 || p.step != 1) return false;
        }
        else if ((p.display_type != PARAM_FLOAT && p.display_type != PARAM_INT) ||
                 !memchr(p.unit, 0, sizeof(p.unit))) return false;
    }
    return true;
}
bool ptfx::validateStrings(const PtfxHeader& h, const uint8_t* code)
{
    for (unsigned i = 0; i < h.num_params; ++i)
    {
        const auto& p = h.params[i];
        if (p.display_type != PARAM_LIST) continue;
        unsigned offset = uint8_t(p.unit[0]) | unsigned(uint8_t(p.unit[1])) << 8;
        for (unsigned item = 0; item < uint8_t(p.unit[2]); ++item)
        {
            if (offset >= h.code_size) return false;
            const auto* end = static_cast<const uint8_t*>(memchr(code + offset, 0, h.code_size - offset));
            if (!end) return false;
            offset = unsigned(end - code) + 1;
        }
    }
    return true;
}
