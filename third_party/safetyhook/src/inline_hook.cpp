#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <optional>
#include <vector>

#if __has_include("Zydis/Zydis.h")
#include "Zydis/Zydis.h"
#elif __has_include("Zydis.h")
#include "Zydis.h"
#else
#error "Zydis not found"
#endif

#include "safetyhook/allocator.hpp"
#include "safetyhook/common.hpp"
#include "safetyhook/os.hpp"
#include "safetyhook/utility.hpp"

#include "safetyhook/inline_hook.hpp"

namespace safetyhook {

#if SAFETYHOOK_ARCH_X86_64
constexpr std::array<uint8_t, 4> kEndbr64{0xF3, 0x0F, 0x1E, 0xFA};
constexpr size_t kTrampolinePrefixSize{kEndbr64.size()};
#else
constexpr size_t kTrampolinePrefixSize{};
#endif

#pragma pack(push, 1)
struct JmpE9 {
    uint8_t opcode{0xE9};
    uint32_t offset{0};
};

#if SAFETYHOOK_ARCH_X86_64
struct JmpFF {
    uint8_t opcode0{0xFF};
    uint8_t opcode1{0x25};
    uint32_t offset{0};
};

struct TrampolineEpilogueE9 {
    JmpE9 jmp_to_original{};
    JmpFF jmp_to_destination{};
    uint64_t destination_address{};
};

struct TrampolineEpilogueFF {
    JmpFF jmp_to_original{};
    uint64_t original_address{};
};
#elif SAFETYHOOK_ARCH_X86_32
struct TrampolineEpilogueE9 {
    JmpE9 jmp_to_original{};
    JmpE9 jmp_to_destination{};
};
#endif
#pragma pack(pop)

static std::optional<int32_t> relative_displacement(uint8_t* target, uint8_t* source, size_t instruction_size) {
    const uintptr_t source_value = reinterpret_cast<uintptr_t>(source);
    if (instruction_size > std::numeric_limits<uintptr_t>::max() - source_value) {
        return std::nullopt;
    }
    const uintptr_t next = source_value + instruction_size;
    const uintptr_t target_value = reinterpret_cast<uintptr_t>(target);
    if (target_value >= next) {
        const uintptr_t distance = target_value - next;
        if (distance > static_cast<uintptr_t>(std::numeric_limits<int32_t>::max())) {
            return std::nullopt;
        }
        return static_cast<int32_t>(distance);
    }
    const uintptr_t distance = next - target_value;
    const uintptr_t negative_limit = static_cast<uintptr_t>(std::numeric_limits<int32_t>::max()) + 1;
    if (distance > negative_limit) {
        return std::nullopt;
    }
    if (distance == negative_limit) {
        return std::numeric_limits<int32_t>::min();
    }
    return -static_cast<int32_t>(distance);
}

#if SAFETYHOOK_ARCH_X86_64
[[nodiscard]] static std::expected<void, InlineHook::Error> emit_jmp_ff(
    uint8_t* src, uint8_t* dst, uint8_t* data, size_t size = sizeof(JmpFF)) {
    if (size < sizeof(JmpFF)) {
        return std::unexpected{InlineHook::Error::not_enough_space(dst)};
    }

    const auto displacement = relative_displacement(data, src, sizeof(JmpFF));
    if (!displacement) {
        return std::unexpected{InlineHook::Error::ip_relative_instruction_out_of_range(src)};
    }

    if (size > sizeof(JmpFF)) {
        std::fill_n(src, size, static_cast<uint8_t>(0x90));
    }

    JmpFF jmp{};
    jmp.offset = static_cast<uint32_t>(*displacement);
    store(data, dst);
    store(src, jmp);

    return {};
}
#endif

[[nodiscard]] static std::expected<void, InlineHook::Error> emit_jmp_e9(
    uint8_t* src, uint8_t* dst, size_t size = sizeof(JmpE9)) {
    if (size < sizeof(JmpE9)) {
        return std::unexpected{InlineHook::Error::not_enough_space(dst)};
    }

    const auto displacement = relative_displacement(dst, src, sizeof(JmpE9));
    if (!displacement) {
        return std::unexpected{InlineHook::Error::ip_relative_instruction_out_of_range(src)};
    }

    if (size > sizeof(JmpE9)) {
        std::fill_n(src, size, static_cast<uint8_t>(0x90));
    }

    JmpE9 jmp{};
    jmp.offset = static_cast<uint32_t>(*displacement);
    store(src, jmp);

    return {};
}

static bool decode(ZydisDecodedInstruction* ix, uint8_t* ip) {
    ZydisDecoder decoder{};
    ZyanStatus status;

#if SAFETYHOOK_ARCH_X86_64
    status = ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
#elif SAFETYHOOK_ARCH_X86_32
    status = ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LEGACY_32, ZYDIS_STACK_WIDTH_32);
#endif

    if (!ZYAN_SUCCESS(status)) {
        return false;
    }

    return ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(&decoder, nullptr, ip, 15, ix));
}

#if SAFETYHOOK_ARCH_X86_32
static std::optional<uint8_t> x86_get_pc_thunk_register(uint8_t* ip, const ZydisDecodedInstruction& ix) {
    if (ix.mnemonic != ZYDIS_MNEMONIC_CALL || ix.raw.imm[0].size != 32) {
        return std::nullopt;
    }

    const auto* thunk_ip = ip + ix.length + static_cast<int32_t>(ix.raw.imm[0].value.s);
    ZydisDecodedInstruction mov_ix{};
    ZydisDecodedInstruction ret_ix{};

    if (!decode(&mov_ix, const_cast<uint8_t*>(thunk_ip)) ||
        !decode(&ret_ix, const_cast<uint8_t*>(thunk_ip + mov_ix.length))) {
        return std::nullopt;
    }

    // GNU i386 PIC thunks have this body: mov r32, [esp]; ret.
    if (mov_ix.mnemonic != ZYDIS_MNEMONIC_MOV || mov_ix.length != 3 || ret_ix.mnemonic != ZYDIS_MNEMONIC_RET ||
        ret_ix.length != 1 || thunk_ip[0] != 0x8B || thunk_ip[2] != 0x24 || thunk_ip[mov_ix.length] != 0xC3) {
        return std::nullopt;
    }

    const auto modrm = thunk_ip[1];

    if ((modrm & 0xC7) != 0x04) {
        return std::nullopt;
    }

    return (modrm >> 3) & 0x07;
}

static void emit_mov_r32_imm32(uint8_t* ip, uint8_t reg, uint8_t* value) {
    *ip = static_cast<uint8_t>(0xB8 + reg);
    store(ip + 1, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value)));
}
#endif

std::expected<InlineHook, InlineHook::Error> InlineHook::create(void* target, void* destination, Flags flags) {
    return create(Allocator::global(), target, destination, flags);
}

std::expected<InlineHook, InlineHook::Error> InlineHook::create(
    const std::shared_ptr<Allocator>& allocator, void* target, void* destination, Flags flags) {
    InlineHook hook{};

    if (const auto setup_result =
            hook.setup(allocator, reinterpret_cast<uint8_t*>(target), reinterpret_cast<uint8_t*>(destination));
        !setup_result) {
        return std::unexpected{setup_result.error()};
    }

    if (!(flags & StartDisabled)) {
        if (auto enable_result = hook.enable(); !enable_result) {
            return std::unexpected{enable_result.error()};
        }
    }

    return hook;
}

InlineHook::InlineHook(InlineHook&& other) noexcept {
    *this = std::move(other);
}

InlineHook& InlineHook::operator=(InlineHook&& other) noexcept {
    if (this != &other) {
        destroy();

        std::scoped_lock lock{m_mutex, other.m_mutex};

        m_target = other.m_target;
        m_patch_target = other.m_patch_target;
        m_destination = other.m_destination;
        m_trampoline = std::move(other.m_trampoline);
        m_trampoline_size = other.m_trampoline_size;
        m_original_bytes = std::move(other.m_original_bytes);
        m_ip_mappings = std::move(other.m_ip_mappings);
        m_trampoline_code_size = other.m_trampoline_code_size;
        m_trampoline_return = other.m_trampoline_return;
        m_trampoline_destination = other.m_trampoline_destination;
        m_enabled = other.m_enabled;
        m_type = other.m_type;

        other.m_target = nullptr;
        other.m_patch_target = nullptr;
        other.m_destination = nullptr;
        other.m_trampoline_size = 0;
        other.m_trampoline_code_size = 0;
        other.m_trampoline_return = nullptr;
        other.m_trampoline_destination = nullptr;
        other.m_enabled = false;
        other.m_type = Type::Unset;
    }

    return *this;
}

InlineHook::~InlineHook() {
    destroy();
}

void InlineHook::reset() {
    *this = {};
}

std::expected<void, InlineHook::Error> InlineHook::setup(
    const std::shared_ptr<Allocator>& allocator, uint8_t* target, uint8_t* destination) {
    m_target = target;
#if SAFETYHOOK_ARCH_X86_64
    m_patch_target = std::equal(kEndbr64.begin(), kEndbr64.end(), target)
        ? target + kEndbr64.size()
        : target;
#else
    m_patch_target = target;
#endif
    m_destination = destination;

    if (auto e9_result = e9_hook(allocator); !e9_result) {
#if SAFETYHOOK_ARCH_X86_64
        if (auto ff_result = ff_hook(allocator); !ff_result) {
            return ff_result;
        }
#elif SAFETYHOOK_ARCH_X86_32
        return e9_result;
#endif
    }

    return {};
}

std::expected<void, InlineHook::Error> InlineHook::e9_hook(const std::shared_ptr<Allocator>& allocator) {
    m_original_bytes.clear();
    m_ip_mappings.clear();
    m_trampoline_code_size = 0;
    m_trampoline_return = nullptr;
    m_trampoline_destination = nullptr;
    m_trampoline_size = kTrampolinePrefixSize + sizeof(TrampolineEpilogueE9);

    struct RelocatedInstruction {
        uint8_t* original{};
        uint8_t* relocated{};
        ZydisDecodedInstruction decoded{};
        size_t emitted_size{};
    };

    std::vector<RelocatedInstruction> instructions;
    std::vector<uint8_t*> desired_addresses{m_patch_target};
    for (auto* ip = m_patch_target; ip < m_patch_target + sizeof(JmpE9);) {
        RelocatedInstruction instruction{};
        instruction.original = ip;
        if (!decode(&instruction.decoded, ip)) {
            return std::unexpected{Error::failed_to_decode_instruction(ip)};
        }

        const auto& ix = instruction.decoded;
        instruction.emitted_size = ix.length;
        if (ix.meta.category == ZYDIS_CATEGORY_COND_BR && ix.meta.branch_type == ZYDIS_BRANCH_TYPE_SHORT) {
            if (ix.opcode < 0x70 || ix.opcode > 0x7F) {
                return std::unexpected{Error::unsupported_instruction_in_trampoline(ip)};
            }
            instruction.emitted_size = 6;
        } else if (ix.meta.category == ZYDIS_CATEGORY_UNCOND_BR &&
            ix.meta.branch_type == ZYDIS_BRANCH_TYPE_SHORT) {
            if (ix.opcode != 0xEB) {
                return std::unexpected{Error::unsupported_instruction_in_trampoline(ip)};
            }
            instruction.emitted_size = 5;
        }
        m_trampoline_size += instruction.emitted_size;
        m_original_bytes.insert(m_original_bytes.end(), ip, ip + ix.length);

        const auto is_relative = (ix.attributes & ZYDIS_ATTRIB_IS_RELATIVE) != 0;
        if (is_relative) {
            if (ix.raw.disp.size == 32) {
                const auto target_address = ip + ix.length + static_cast<int32_t>(ix.raw.disp.value);
                desired_addresses.emplace_back(target_address);
            } else if (ix.raw.imm[0].size == 32) {
                const auto target_address = ip + ix.length + static_cast<int32_t>(ix.raw.imm[0].value.s);
                desired_addresses.emplace_back(target_address);
            } else if (ix.meta.category == ZYDIS_CATEGORY_COND_BR && ix.meta.branch_type == ZYDIS_BRANCH_TYPE_SHORT) {
                const auto target_address = ip + ix.length + static_cast<int32_t>(ix.raw.imm[0].value.s);
                desired_addresses.emplace_back(target_address);
            } else if (ix.meta.category == ZYDIS_CATEGORY_UNCOND_BR && ix.meta.branch_type == ZYDIS_BRANCH_TYPE_SHORT) {
                const auto target_address = ip + ix.length + static_cast<int32_t>(ix.raw.imm[0].value.s);
                desired_addresses.emplace_back(target_address);
            } else {
                return std::unexpected{Error::unsupported_instruction_in_trampoline(ip)};
            }
        }
        instructions.push_back(instruction);
        ip += ix.length;
    }

    auto trampoline_allocation = allocator->allocate_near(desired_addresses, m_trampoline_size);

    if (!trampoline_allocation) {
        return std::unexpected{Error::bad_allocation(trampoline_allocation.error())};
    }

    m_trampoline = std::move(*trampoline_allocation);
#if SAFETYHOOK_ARCH_X86_64
    std::copy(kEndbr64.begin(), kEndbr64.end(), m_trampoline.data());
#endif
    auto* relocated = m_trampoline.data() + kTrampolinePrefixSize;
    for (auto& instruction : instructions) {
        instruction.relocated = relocated;
        m_ip_mappings.push_back({instruction.original, relocated});
        relocated += instruction.emitted_size;
    }

    const auto map_control_target = [&instructions, this](uint8_t* address) -> std::optional<uint8_t*> {
        const auto value = reinterpret_cast<uintptr_t>(address);
        const auto target = reinterpret_cast<uintptr_t>(m_patch_target);
        if (value < target || value - target >= m_original_bytes.size()) {
            return address;
        }
        const auto found = std::find_if(instructions.begin(), instructions.end(), [address](const auto& instruction) {
            return instruction.original == address;
        });
        if (found == instructions.end()) {
            return std::nullopt;
        }
        return found->relocated;
    };

    for (const auto& instruction : instructions) {
        auto* ip = instruction.original;
        auto* tramp_ip = instruction.relocated;
        const auto& ix = instruction.decoded;
        const auto is_relative = (ix.attributes & ZYDIS_ATTRIB_IS_RELATIVE) != 0;

#if SAFETYHOOK_ARCH_X86_32
        if (auto thunk_reg = x86_get_pc_thunk_register(ip, ix); thunk_reg) {
            emit_mov_r32_imm32(tramp_ip, *thunk_reg, ip + ix.length);
            tramp_ip += ix.length;
            continue;
        }
#endif

        if (is_relative && ix.raw.disp.size == 32) {
            std::copy_n(ip, ix.length, tramp_ip);
            const auto target_address = ip + ix.length + ix.raw.disp.value;
            const auto target_value = reinterpret_cast<uintptr_t>(target_address);
            const auto original_value = reinterpret_cast<uintptr_t>(m_patch_target);
            if (target_value >= original_value && target_value - original_value < m_original_bytes.size()) {
                return std::unexpected{Error::unsupported_instruction_in_trampoline(ip)};
            }
            const auto new_disp = relative_displacement(target_address, tramp_ip, ix.length);
            if (!new_disp) {
                return std::unexpected{Error::unsupported_instruction_in_trampoline(ip)};
            }
            store(tramp_ip + ix.raw.disp.offset, *new_disp);
        } else if (is_relative && ix.raw.imm[0].size == 32) {
            std::copy_n(ip, ix.length, tramp_ip);
            const auto target_address = ip + ix.length + ix.raw.imm[0].value.s;
            const auto mapped = map_control_target(target_address);
            if (!mapped) {
                return std::unexpected{Error::unsupported_instruction_in_trampoline(ip)};
            }
            const auto new_disp = relative_displacement(*mapped, tramp_ip, ix.length);
            if (!new_disp) {
                return std::unexpected{Error::unsupported_instruction_in_trampoline(ip)};
            }
            store(tramp_ip + ix.raw.imm[0].offset, *new_disp);
        } else if (ix.meta.category == ZYDIS_CATEGORY_COND_BR && ix.meta.branch_type == ZYDIS_BRANCH_TYPE_SHORT) {
            const auto target_address = ip + ix.length + ix.raw.imm[0].value.s;
            const auto mapped = map_control_target(target_address);
            if (!mapped) {
                return std::unexpected{Error::unsupported_instruction_in_trampoline(ip)};
            }
            const auto new_disp = relative_displacement(*mapped, tramp_ip, 6);
            if (!new_disp) {
                return std::unexpected{Error::unsupported_instruction_in_trampoline(ip)};
            }

            *tramp_ip = 0x0F;
            *(tramp_ip + 1) = 0x10 + ix.opcode;
            store(tramp_ip + 2, *new_disp);
        } else if (ix.meta.category == ZYDIS_CATEGORY_UNCOND_BR && ix.meta.branch_type == ZYDIS_BRANCH_TYPE_SHORT) {
            const auto target_address = ip + ix.length + ix.raw.imm[0].value.s;
            const auto mapped = map_control_target(target_address);
            if (!mapped) {
                return std::unexpected{Error::unsupported_instruction_in_trampoline(ip)};
            }
            const auto new_disp = relative_displacement(*mapped, tramp_ip, 5);
            if (!new_disp) {
                return std::unexpected{Error::unsupported_instruction_in_trampoline(ip)};
            }

            *tramp_ip = 0xE9;
            store(tramp_ip + 1, *new_disp);
        } else {
            std::copy_n(ip, ix.length, tramp_ip);
        }
    }

    auto trampoline_epilogue = reinterpret_cast<TrampolineEpilogueE9*>(
        m_trampoline.address() + m_trampoline_size - sizeof(TrampolineEpilogueE9));
    m_trampoline_code_size = reinterpret_cast<uint8_t*>(trampoline_epilogue) - m_trampoline.data();
    m_trampoline_return = reinterpret_cast<uint8_t*>(&trampoline_epilogue->jmp_to_original);
    m_trampoline_destination = reinterpret_cast<uint8_t*>(&trampoline_epilogue->jmp_to_destination);

    // jmp from trampoline to original.
    auto src = reinterpret_cast<uint8_t*>(&trampoline_epilogue->jmp_to_original);
    auto dst = m_patch_target + m_original_bytes.size();

    if (auto result = emit_jmp_e9(src, dst); !result) {
        return std::unexpected{result.error()};
    }

    // jmp from trampoline to destination.
    src = reinterpret_cast<uint8_t*>(&trampoline_epilogue->jmp_to_destination);
    dst = m_destination;

#if SAFETYHOOK_ARCH_X86_64
    auto data = reinterpret_cast<uint8_t*>(&trampoline_epilogue->destination_address);

    if (auto result = emit_jmp_ff(src, dst, data); !result) {
        return std::unexpected{result.error()};
    }
#elif SAFETYHOOK_ARCH_X86_32
    if (auto result = emit_jmp_e9(src, dst); !result) {
        return std::unexpected{result.error()};
    }
#endif

    m_type = Type::E9;

    return {};
}

#if SAFETYHOOK_ARCH_X86_64
std::expected<void, InlineHook::Error> InlineHook::ff_hook(const std::shared_ptr<Allocator>& allocator) {
    m_original_bytes.clear();
    m_ip_mappings.clear();
    m_trampoline_code_size = 0;
    m_trampoline_return = nullptr;
    m_trampoline_destination = nullptr;
    m_trampoline_size = kTrampolinePrefixSize + sizeof(TrampolineEpilogueFF);
    ZydisDecodedInstruction ix{};

    for (auto ip = m_patch_target; ip < m_patch_target + sizeof(JmpFF) + sizeof(uintptr_t); ip += ix.length) {
        if (!decode(&ix, ip)) {
            return std::unexpected{Error::failed_to_decode_instruction(ip)};
        }

        // We can't support any instruction that is IP relative here because
        // ff_hook should only be called if e9_hook failed indicating that
        // we're likely outside the +- 2GB range.
        if (ix.attributes & ZYDIS_ATTRIB_IS_RELATIVE) {
            return std::unexpected{Error::ip_relative_instruction_out_of_range(ip)};
        }

        m_original_bytes.insert(m_original_bytes.end(), ip, ip + ix.length);
        m_trampoline_size += ix.length;
    }

    auto trampoline_allocation = allocator->allocate(m_trampoline_size);

    if (!trampoline_allocation) {
        return std::unexpected{Error::bad_allocation(trampoline_allocation.error())};
    }

    m_trampoline = std::move(*trampoline_allocation);
#if SAFETYHOOK_ARCH_X86_64
    std::copy(kEndbr64.begin(), kEndbr64.end(), m_trampoline.data());
#endif

    for (auto ip = m_patch_target, tramp_ip = m_trampoline.data() + kTrampolinePrefixSize;
         ip < m_patch_target + m_original_bytes.size(); ip += ix.length) {
        if (!decode(&ix, ip)) {
            m_trampoline.free();
            return std::unexpected{Error::failed_to_decode_instruction(ip)};
        }
        m_ip_mappings.push_back({ip, tramp_ip});
        std::copy_n(ip, ix.length, tramp_ip);
        tramp_ip += ix.length;
    }

    const auto trampoline_epilogue =
        reinterpret_cast<TrampolineEpilogueFF*>(m_trampoline.data() + m_trampoline_size - sizeof(TrampolineEpilogueFF));
    m_trampoline_code_size = reinterpret_cast<uint8_t*>(trampoline_epilogue) - m_trampoline.data();
    m_trampoline_return = reinterpret_cast<uint8_t*>(&trampoline_epilogue->jmp_to_original);

    // jmp from trampoline to original.
    auto src = reinterpret_cast<uint8_t*>(&trampoline_epilogue->jmp_to_original);
    auto dst = m_patch_target + m_original_bytes.size();
    auto data = reinterpret_cast<uint8_t*>(&trampoline_epilogue->original_address);

    if (auto result = emit_jmp_ff(src, dst, data); !result) {
        return std::unexpected{result.error()};
    }

    m_type = Type::FF;

    return {};
}
#endif

std::expected<void, InlineHook::Error> InlineHook::enable() {
    std::scoped_lock lock{m_mutex};

    if (m_enabled) {
        return {};
    }

    std::optional<Error> error;

    // jmp from original to trampoline.
    const std::array hazardous_ranges{IpRange{m_patch_target, m_original_bytes.size()}};
    const bool trapped = trap_threads(
        m_patch_target, m_original_bytes.size(), m_ip_mappings, hazardous_ranges, [this, &error] {
        if (m_type == Type::E9) {
            auto trampoline_epilogue = reinterpret_cast<TrampolineEpilogueE9*>(
                m_trampoline.address() + m_trampoline_size - sizeof(TrampolineEpilogueE9));

            if (auto result = emit_jmp_e9(m_patch_target,
                    reinterpret_cast<uint8_t*>(&trampoline_epilogue->jmp_to_destination), m_original_bytes.size());
                !result) {
                error = result.error();
            }
        }

#if SAFETYHOOK_ARCH_X86_64
        if (m_type == Type::FF) {
            if (auto result = emit_jmp_ff(
                    m_patch_target, m_destination, m_patch_target + sizeof(JmpFF), m_original_bytes.size());
                !result) {
                error = result.error();
            }
        }
#endif
    });

    if (!trapped) {
        return std::unexpected{Error::failed_to_unprotect(m_patch_target)};
    }

    if (error) {
        return std::unexpected{*error};
    }

    m_enabled = true;

    return {};
}

std::expected<void, InlineHook::Error> InlineHook::disable(std::span<const IpRange> extra_hazardous_ranges) {
    std::scoped_lock lock{m_mutex};

    if (!m_enabled) {
        return {};
    }

    std::vector<IpMapping> mappings;
    mappings.reserve(m_ip_mappings.size() + 4);
    mappings.push_back({m_patch_target, m_patch_target});
    mappings.push_back({m_trampoline.data(), m_target});
    for (const auto& mapping : m_ip_mappings) {
        mappings.push_back({mapping.to, mapping.from});
    }
    mappings.push_back({m_trampoline_return, m_patch_target + m_original_bytes.size()});
    if (m_trampoline_destination) {
        mappings.push_back({m_trampoline_destination, m_patch_target});
    }
    size_t epilogue_code_size{};
    if (m_type == Type::E9) {
#if SAFETYHOOK_ARCH_X86_64
        epilogue_code_size = sizeof(JmpE9) + sizeof(JmpFF);
#elif SAFETYHOOK_ARCH_X86_32
        epilogue_code_size = sizeof(JmpE9) * 2;
#endif
    }
#if SAFETYHOOK_ARCH_X86_64
    if (m_type == Type::FF) {
        epilogue_code_size = sizeof(JmpFF);
    }
#endif
    std::vector<IpRange> hazardous_ranges;
    hazardous_ranges.reserve(extra_hazardous_ranges.size() + 2);
    hazardous_ranges.push_back({m_patch_target, m_original_bytes.size()});
    hazardous_ranges.push_back({m_trampoline.data(), m_trampoline_code_size + epilogue_code_size});
    hazardous_ranges.insert(
        hazardous_ranges.end(),
        extra_hazardous_ranges.begin(),
        extra_hazardous_ranges.end());
    if (!trap_threads(m_patch_target, m_original_bytes.size(), mappings, hazardous_ranges,
            [this] { std::copy(m_original_bytes.begin(), m_original_bytes.end(), m_patch_target); })) {
        return std::unexpected{Error::failed_to_unprotect(m_patch_target)};
    }

    m_enabled = false;

    return {};
}

std::expected<void, InlineHook::Error> InlineHook::quiesce(
    std::span<const IpRange> extra_hazardous_ranges) {
    std::scoped_lock lock{m_mutex};

    if (m_enabled || !m_trampoline || m_original_bytes.empty()) {
        return std::unexpected{Error::failed_to_unprotect(m_patch_target)};
    }

    size_t epilogue_code_size{};
    if (m_type == Type::E9) {
#if SAFETYHOOK_ARCH_X86_64
        epilogue_code_size = sizeof(JmpE9) + sizeof(JmpFF);
#elif SAFETYHOOK_ARCH_X86_32
        epilogue_code_size = sizeof(JmpE9) * 2;
#endif
    }
#if SAFETYHOOK_ARCH_X86_64
    if (m_type == Type::FF) {
        epilogue_code_size = sizeof(JmpFF);
    }
#endif

    std::vector<IpRange> hazardous_ranges;
    hazardous_ranges.reserve(extra_hazardous_ranges.size() + 1);
    hazardous_ranges.push_back({m_trampoline.data(), m_trampoline_code_size + epilogue_code_size});
    hazardous_ranges.insert(
        hazardous_ranges.end(),
        extra_hazardous_ranges.begin(),
        extra_hazardous_ranges.end());
    const std::span<const IpMapping> mappings;
    if (!trap_threads(
            m_patch_target,
            m_original_bytes.size(),
            mappings,
            hazardous_ranges,
            [] {})) {
        return std::unexpected{Error::failed_to_unprotect(m_patch_target)};
    }
    return {};
}

void InlineHook::destroy() {
    [[maybe_unused]] auto disable_result = disable();

    std::scoped_lock lock{m_mutex};

    if (!m_trampoline) {
        return;
    }

    m_trampoline.free();
}
} // namespace safetyhook
