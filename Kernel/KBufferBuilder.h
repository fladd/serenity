/*
 * Copyright (c) 2018-2021, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/StringView.h>
#include <Kernel/KBuffer.h>
#include <stdarg.h>

namespace Kernel {

class KBufferBuilder {
    AK_MAKE_NONCOPYABLE(KBufferBuilder);

public:
    using OutputType = KBuffer;

    static KResultOr<KBufferBuilder> try_create();

    KBufferBuilder(KBufferBuilder&&) = default;
    KBufferBuilder& operator=(KBufferBuilder&&) = default;
    ~KBufferBuilder() = default;

    KResult append(const StringView&);
    KResult append(char);
    KResult append(const char*, int);

    KResult append_escaped_for_json(const StringView&);
    KResult append_bytes(ReadonlyBytes);

    template<typename... Parameters>
    KResult appendff(CheckedFormatString<Parameters...>&& fmtstr, const Parameters&... parameters)
    {
        // FIXME: This really not ideal, but vformat expects StringBuilder.
        StringBuilder builder;
        AK::VariadicFormatParams variadic_format_params { parameters... };
        vformat(builder, fmtstr.view(), variadic_format_params);
        return append_bytes(builder.string_view().bytes());
    }

    bool flush();
    OwnPtr<KBuffer> build();

    ReadonlyBytes bytes() const
    {
        if (!m_buffer)
            return {};
        return ReadonlyBytes { m_buffer->data(), m_buffer->size() };
    }

private:
    explicit KBufferBuilder(NonnullOwnPtr<KBuffer>);

    bool check_expand(size_t);
    u8* insertion_ptr()
    {
        if (!m_buffer)
            return nullptr;
        return m_buffer->data() + m_size;
    }

    OwnPtr<KBuffer> m_buffer;
    size_t m_size { 0 };
};

}
