if(NOT SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()
if(EXISTS "${SOURCE_ROOT}/src/iiJson")
    message(FATAL_ERROR "The source tree must not repeat the project name")
endif()
foreach(_file Json.h Json.cpp Value.h Value.cpp Schema/Schema.h Schema/Schema.cpp
        Schema/SchemaDefinition.cpp Ontology/Ontology.h Ontology/Ontology.cpp
        Ontology/OntologyCodec.cpp Ontology/Snapshot.cpp detail/SchemaNode.h
        detail/OntologyData.h detail/ContractIO.h)
    if(NOT EXISTS "${SOURCE_ROOT}/src/${_file}")
        message(FATAL_ERROR "Missing source in its responsibility directory: ${_file}")
    endif()
endforeach()
