#define DUCKDB_EXTENSION_MAIN

#include "postal_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "duckdb/main/extension_util.hpp"
#include "libpostal/libpostal.h"

namespace duckdb {

//------------------------------------------------------------------------------
// Setup and initialization (once)
//------------------------------------------------------------------------------

std::once_flag postal_initialized_flag;

static void InitializeLibPostal() {
    std::call_once(postal_initialized_flag, []{
        // Set libpostal data directory
        if (!libpostal_setup() || !libpostal_setup_parser() || !libpostal_setup_language_classifier()) {
            throw InternalException("Could not initialize libpostal");
        }
    });
}

//------------------------------------------------------------------------------
// Postal functions
//------------------------------------------------------------------------------

static void PostalParseAllFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input = args.data[0];

    libpostal_address_parser_options_t options = libpostal_get_address_parser_default_options();

    UnifiedVectorFormat input_format;
    input.ToUnifiedFormat(count, input_format);;
    auto input_data = UnifiedVectorFormat::GetData<string_t>(input_format);
    auto input_sel = input_format.sel;
    
    auto result_entries = ListVector::GetData(result);
    auto &key_vector = MapVector::GetKeys(result);
    auto &value_vector = MapVector::GetValues(result);

    auto total_component_count = 0;
    
    for(idx_t out_idx = 0; out_idx < count; out_idx++) {
        auto in_idx = input_sel->get_index(out_idx);
        auto input_str = input_data[in_idx].GetString();

        auto parsed_address = libpostal_parse_address(const_cast<char*>(input_str.c_str()), options);
        if(parsed_address == nullptr) {
            throw InvalidInputException(StringUtil::Format("Could not parse address: %s", input_str.c_str()));
        }

        // Create a list entry for the current address
        auto &list_entry = result_entries[out_idx];
        list_entry.offset = total_component_count;
        list_entry.length = parsed_address->num_components;

        // Reserve space for the components
        total_component_count += parsed_address->num_components;
        ListVector::Reserve(result, total_component_count);

        auto key_data = FlatVector::GetData<string_t>(key_vector);
        auto value_data = FlatVector::GetData<string_t>(value_vector);

        for(size_t i = 0; i < parsed_address->num_components; i++) {
            key_data[list_entry.offset + i] = StringVector::AddString(key_vector, parsed_address->labels[i]);
            value_data[list_entry.offset + i] = StringVector::AddString(value_vector, parsed_address->components[i]);
        }

        libpostal_address_parser_response_destroy(parsed_address);
    }

    ListVector::SetListSize(result, total_component_count);
    
    if(args.AllConstant()) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

static void PostalNormalizeFunc(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input = args.data[0];

    UnifiedVectorFormat input_format;
    input.ToUnifiedFormat(count, input_format);;
    auto input_data = UnifiedVectorFormat::GetData<string_t>(input_format);
    auto input_sel = input_format.sel;

    auto &result_entry = ListVector::GetEntry(result);
    auto result_entries = ListVector::GetData(result);

    idx_t total_expansion_count = 0;
    
    libpostal_normalize_options_t options = libpostal_get_default_options();

    for(idx_t out_idx = 0; out_idx < count; out_idx++) {
        auto in_idx = input_sel->get_index(out_idx);
        auto input_str = input_data[in_idx].GetString();

        size_t num_expansions;
        char **expansions = libpostal_expand_address(const_cast<char*>(input_str.c_str()), options, &num_expansions);

        if(expansions == nullptr) {
            throw InvalidInputException(StringUtil::Format("Could not expand address: %s", input_str.c_str()));
        }

        // Create a list entry for the current address
        auto &list_entry = result_entries[out_idx];
        list_entry.offset = total_expansion_count;
        list_entry.length = num_expansions;

        // Reserve space for the components
        total_expansion_count += num_expansions;
        ListVector::Reserve(result, total_expansion_count);

        auto result_data = FlatVector::GetData<string_t>(result_entry);
        for(size_t i = 0; i < num_expansions; i++) {
            result_data[list_entry.offset + i] = StringVector::AddString(result, expansions[i]);
        }

        libpostal_expansion_array_destroy(expansions, num_expansions);
    }

    // Set the list size
    ListVector::SetListSize(result, total_expansion_count);
    
    if(args.AllConstant()) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

//------------------------------------------------------------------------------
// Load extension
//------------------------------------------------------------------------------

static void LoadInternal(DatabaseInstance &instance) {

    InitializeLibPostal();

    // Setup config options

	auto &config = DBConfig::GetConfig(instance);

    // TODO: Fork libpostal and make it threadsafe. We cant even add error handlers now.
	// Global data directory config
	config.AddExtensionOption("postal_data_dir", "LibPostal data directory", LogicalType::VARCHAR, Value(""), 
    [](ClientContext &context, SetScope scope, Value &parameter) {
        auto path = StringValue::Get(parameter);
        if(path.empty()) {
            throw InvalidInputException("postal_data_dir cannot be empty");
        }

        // Join the path with the current working directory if it is not absolute
        auto &fs = context.db->GetFileSystem();
        if(!fs.IsPathAbsolute(path)) {
            path = fs.JoinPath(fs.GetWorkingDirectory(), path);
        }

        // Check if the directory exists
        if(!fs.DirectoryExists(path)) {
            throw InvalidInputException(StringUtil::Format("postal_data_dir does not exist: %s", path.c_str()));
        }

        // Set the data directory
        auto ok = libpostal_setup_datadir(const_cast<char*>(path.c_str()));
        if(!ok) {
            throw InvalidInputException(StringUtil::Format("Could not set postal_data_dir: %s", path.c_str()));
        }
    });

    // Setup functions
    ScalarFunctionSet postal_parse_func("postal_parse");
    postal_parse_func.AddFunction(ScalarFunction({LogicalType::VARCHAR}, LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR), PostalParseAllFunction));
    //postal_parse_func.AddFunction(ScalarFunction({LogicalType::VARCHAR, LogicalType::LIST(LogicalType::VARCHAR)}, LogicalTypeId::STRUCT, PostalParseLabelsFunction));

    ExtensionUtil::RegisterFunction(instance, postal_parse_func);

    auto normalize_func = ScalarFunction("postal_normalize", {LogicalType::VARCHAR}, LogicalType::VARCHAR, PostalNormalizeFunc);
    ExtensionUtil::RegisterFunction(instance, normalize_func);
}

void PostalExtension::Load(DuckDB &db) {
	LoadInternal(*db.instance);
}
std::string PostalExtension::Name() {
	return "postal";
}

} // namespace duckdb

extern "C" {

DUCKDB_EXTENSION_API void postal_init(duckdb::DatabaseInstance &db) {
	LoadInternal(db);
}

DUCKDB_EXTENSION_API const char *postal_version() {
	return duckdb::DuckDB::LibraryVersion();
}
}

#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN not defined
#endif
