/*
 * Copyright (c) 2021, Tim Flynn <trflynn89@pm.me>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/AllOf.h>
#include <AK/CharacterTypes.h>
#include <AK/Format.h>
#include <AK/HashMap.h>
#include <AK/JsonObject.h>
#include <AK/JsonParser.h>
#include <AK/JsonValue.h>
#include <AK/LexicalPath.h>
#include <AK/QuickSort.h>
#include <AK/SourceGenerator.h>
#include <AK/String.h>
#include <AK/StringBuilder.h>
#include <LibCore/ArgsParser.h>
#include <LibCore/DirIterator.h>
#include <LibCore/File.h>

struct Locale {
    String language;
    Optional<String> territory;
    Optional<String> variant;
    HashMap<String, String> territories;
};

struct UnicodeLocaleData {
    HashMap<String, Locale> locales;
    Vector<String> languages;
    Vector<String> territories;
    Vector<String> variants;
};

static void write_to_file_if_different(Core::File& file, StringView contents)
{
    auto const current_contents = file.read_all();

    if (StringView { current_contents.bytes() } == contents)
        return;

    VERIFY(file.seek(0));
    VERIFY(file.truncate(0));
    VERIFY(file.write(contents));
}

static void parse_identity(String locale_path, UnicodeLocaleData& locale_data, Locale& locale)
{
    LexicalPath languages_path(move(locale_path)); // Note: Every JSON file defines identity data, so we can use any of them.
    languages_path = languages_path.append("languages.json"sv);
    VERIFY(Core::File::exists(languages_path.string()));

    auto languages_file_or_error = Core::File::open(languages_path.string(), Core::OpenMode::ReadOnly);
    VERIFY(!languages_file_or_error.is_error());

    auto languages = JsonParser(languages_file_or_error.value()->read_all()).parse();
    VERIFY(languages.has_value());

    auto const& main_object = languages->as_object().get("main"sv);
    auto const& locale_object = main_object.as_object().get(languages_path.parent().basename());
    auto const& identity_object = locale_object.as_object().get("identity"sv);
    auto const& language_string = identity_object.as_object().get("language"sv);
    auto const& territory_string = identity_object.as_object().get("territory"sv);
    auto const& variant_string = identity_object.as_object().get("variant"sv);

    locale.language = language_string.as_string();
    if (!locale_data.languages.contains_slow(locale.language))
        locale_data.languages.append(locale.language);

    if (territory_string.is_string()) {
        locale.territory = territory_string.as_string();
        if (!locale_data.territories.contains_slow(*locale.territory))
            locale_data.territories.append(*locale.territory);
    }

    if (variant_string.is_string()) {
        locale.variant = variant_string.as_string();
        if (!locale_data.variants.contains_slow(*locale.variant))
            locale_data.variants.append(*locale.variant);
    }
}

static void parse_locale_territories(String locale_path, Locale& locale)
{
    LexicalPath territories_path(move(locale_path));
    territories_path = territories_path.append("territories.json"sv);
    VERIFY(Core::File::exists(territories_path.string()));

    auto territories_file_or_error = Core::File::open(territories_path.string(), Core::OpenMode::ReadOnly);
    VERIFY(!territories_file_or_error.is_error());

    auto territories = JsonParser(territories_file_or_error.value()->read_all()).parse();
    VERIFY(territories.has_value());

    auto const& main_object = territories->as_object().get("main"sv);
    auto const& locale_object = main_object.as_object().get(territories_path.parent().basename());
    auto const& locale_display_names_object = locale_object.as_object().get("localeDisplayNames"sv);
    auto const& territories_object = locale_display_names_object.as_object().get("territories"sv);

    territories_object.as_object().for_each_member([&](auto const& key, JsonValue const& value) {
        locale.territories.set(key, value.as_string());
    });
}

static void parse_all_locales(String locale_names_path, UnicodeLocaleData& locale_data)
{
    LexicalPath locale_names(move(locale_names_path));
    locale_names = locale_names.append("main"sv);
    VERIFY(Core::File::is_directory(locale_names.string()));

    Core::DirIterator iterator(locale_names.string(), Core::DirIterator::SkipParentAndBaseDir);
    if (iterator.has_error()) {
        warnln("{}: {}", locale_names.string(), iterator.error_string());
        VERIFY_NOT_REACHED();
    }

    while (iterator.has_next()) {
        auto locale_path = iterator.next_full_path();
        VERIFY(Core::File::is_directory(locale_path));

        auto& locale = locale_data.locales.ensure(LexicalPath::basename(locale_path));
        parse_identity(locale_path, locale_data, locale);
        parse_locale_territories(locale_path, locale);
    }
}

static String format_identifier(StringView owner, String identifier)
{
    identifier.replace("-"sv, "_"sv, true);

    if (all_of(identifier, is_ascii_digit))
        return String::formatted("{}_{}", owner[0], identifier);
    return identifier.to_titlecase();
}

static void generate_unicode_locale_header(Core::File& file, UnicodeLocaleData& locale_data)
{
    StringBuilder builder;
    SourceGenerator generator { builder };

    auto generate_enum = [&](StringView name, StringView default_, Vector<String>& values) {
        quick_sort(values);

        generator.set("name", name);
        generator.set("underlying", ((values.size() + !default_.is_empty()) < 256) ? "u8"sv : "u16"sv);

        generator.append(R"~~~(
enum class @name@ : @underlying@ {)~~~");

        if (!default_.is_empty()) {
            generator.set("default", default_);
            generator.append(R"~~~(
    @default@,)~~~");
        }

        for (auto const& value : values) {
            generator.set("value", format_identifier(name, value));
            generator.append(R"~~~(
    @value@,)~~~");
        }

        generator.append(R"~~~(
};
)~~~");
    };

    generator.append(R"~~~(
#pragma once

#include <AK/HashMap.h>
#include <AK/Optional.h>
#include <AK/Span.h>
#include <AK/String.h>
#include <AK/Types.h>
#include <LibUnicode/Forward.h>

namespace Unicode {
)~~~");

    auto locales = locale_data.locales.keys();
    generate_enum("Locale"sv, "None"sv, locales);
    generate_enum("Language"sv, {}, locale_data.languages);
    generate_enum("Territory"sv, {}, locale_data.territories);
    generate_enum("Variant"sv, {}, locale_data.variants);

    generator.append(R"~~~(
struct LocaleData {
    Language language;
    Optional<Territory> territory;
    Optional<Variant> variant;
    Span<StringView const> territories;
};

using LocaleMap = HashMap<String, LocaleData>;

namespace Detail {

LocaleMap const& available_locales();

Optional<Locale> locale_from_string(StringView const& locale);
Optional<Language> language_from_string(StringView const& language);
Optional<Territory> territory_from_string(StringView const& territory);

}

}
)~~~");

    write_to_file_if_different(file, generator.as_string_view());
}

static void generate_unicode_locale_implementation(Core::File& file, UnicodeLocaleData& locale_data)
{
    StringBuilder builder;
    SourceGenerator generator { builder };
    generator.set("locales_size"sv, String::number(locale_data.locales.size()));
    generator.set("territories_size", String::number(locale_data.territories.size()));

    generator.append(R"~~~(
#include <AK/Array.h>
#include <LibUnicode/UnicodeLocale.h>

namespace Unicode {

)~~~");

    auto format_mapping_name = [](StringView format, StringView name) {
        auto mapping_name = name.to_lowercase_string();
        mapping_name.replace("-"sv, "_"sv, true);
        return String::formatted(format, mapping_name);
    };

    auto append_mapping_list = [&](String name, auto const& keys, auto const& mappings) {
        generator.set("name", name);
        generator.append(R"~~~(
static constexpr Array<StringView, @territories_size@> @name@ { {)~~~");

        for (auto const& key : keys) {
            auto it = mappings.find(key);
            VERIFY(it != mappings.end());

            generator.set("mapping"sv, it->value);
            generator.append(R"~~~(
    "@mapping@"sv,)~~~");
        }

        generator.append(R"~~~(
} };
)~~~");
    };

    for (auto const& locale : locale_data.locales) {
        auto mapping_name = format_mapping_name("s_territories_{}", locale.key);
        append_mapping_list(move(mapping_name), locale_data.territories, locale.value.territories);
    }

    generator.append(R"~~~(
static LocaleMap const& ensure_locale_map()
{
    static LocaleMap locale_map {};
    locale_map.ensure_capacity(@locales_size@);
)~~~");

    for (auto const& locale : locale_data.locales) {
        auto mapping_name = format_mapping_name("s_territories_{}", locale.key);
        generator.set("mapping_name"sv, move(mapping_name));
        generator.set("locale"sv, locale.key);
        generator.set("language"sv, String::formatted("Language::{}", format_identifier("Language"sv, locale.value.language)));

        if (locale.value.territory.has_value())
            generator.set("territory"sv, String::formatted("Territory::{}", format_identifier("Territory"sv, *locale.value.territory)));
        else
            generator.set("territory"sv, "{}"sv);

        if (locale.value.variant.has_value())
            generator.set("variant"sv, String::formatted("Variant::{}", format_identifier("Variant"sv, *locale.value.variant)));
        else
            generator.set("variant"sv, "{}"sv);

        generator.append(R"~~~(
    locale_map.set("@locale@"sv, { @language@, @territory@, @variant@, @mapping_name@.span() });)~~~");
    }

    generator.append(R"~~~(

    return locale_map;
}

namespace Detail {

LocaleMap const& available_locales()
{
    static auto const& locale_map = ensure_locale_map();
    return locale_map;
}
)~~~");

    auto append_from_string = [&](StringView enum_title, StringView enum_snake, Vector<String> const& values) {
        generator.set("enum_title", enum_title);
        generator.set("enum_snake", enum_snake);

        generator.append(R"~~~(
Optional<@enum_title@> @enum_snake@_from_string(StringView const& @enum_snake@)
{
    static HashMap<String, @enum_title@> @enum_snake@_values { {)~~~");

        for (auto const& value : values) {
            generator.set("key"sv, value);
            generator.set("value"sv, format_identifier(enum_title, value));

            generator.append(R"~~~(
        { "@key@"sv, @enum_title@::@value@ },)~~~");
        }

        generator.append(R"~~~(
    } };

    if (auto value = @enum_snake@_values.get(@enum_snake@); value.has_value())
        return value.value();
    return {};
}
)~~~");
    };

    append_from_string("Locale"sv, "locale"sv, locale_data.locales.keys());
    append_from_string("Language"sv, "language"sv, locale_data.languages);
    append_from_string("Territory"sv, "territory"sv, locale_data.territories);

    generator.append(R"~~~(
}

}
)~~~");

    write_to_file_if_different(file, generator.as_string_view());
}

int main(int argc, char** argv)
{
    char const* generated_header_path = nullptr;
    char const* generated_implementation_path = nullptr;
    char const* locale_names_path = nullptr;

    Core::ArgsParser args_parser;
    args_parser.add_option(generated_header_path, "Path to the Unicode locale header file to generate", "generated-header-path", 'h', "generated-header-path");
    args_parser.add_option(generated_implementation_path, "Path to the Unicode locale implementation file to generate", "generated-implementation-path", 'c', "generated-implementation-path");
    args_parser.add_option(locale_names_path, "Path to cldr-localenames directory", "locale-names-path", 'l', "locale-names-path");
    args_parser.parse(argc, argv);

    auto open_file = [&](StringView path, StringView flags, Core::OpenMode mode = Core::OpenMode::ReadOnly) {
        if (path.is_empty()) {
            warnln("{} is required", flags);
            args_parser.print_usage(stderr, argv[0]);
            exit(1);
        }

        auto file_or_error = Core::File::open(path, mode);
        if (file_or_error.is_error()) {
            warnln("Failed to open {}: {}", path, file_or_error.release_error());
            exit(1);
        }

        return file_or_error.release_value();
    };

    auto generated_header_file = open_file(generated_header_path, "-h/--generated-header-path", Core::OpenMode::ReadWrite);
    auto generated_implementation_file = open_file(generated_implementation_path, "-c/--generated-implementation-path", Core::OpenMode::ReadWrite);

    UnicodeLocaleData locale_data;
    parse_all_locales(locale_names_path, locale_data);

    generate_unicode_locale_header(generated_header_file, locale_data);
    generate_unicode_locale_implementation(generated_implementation_file, locale_data);

    return 0;
}
