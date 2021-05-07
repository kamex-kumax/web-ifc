/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
 
#include <stdio.h>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <stack>

#include <emscripten/bind.h>

#include "include/web-ifc.h"
#include "include/web-ifc-geometry.h"

std::vector<webifc::IfcLoader> loaders;
std::vector<webifc::IfcGeometryLoader> geomLoaders;

// use to construct API placeholders
int main() {
    loaders.emplace_back();
    geomLoaders.emplace_back(loaders[0]);

    return 0;
}

std::string ReadFile(const std::string& filename)
{
    std::ifstream t("/" + filename);
    t.seekg(0, std::ios::end);
    size_t size = t.tellg();
    std::string buffer(size, ' ');
    t.seekg(0);
    t.read(&buffer[0], size);
    return buffer;
}

void WriteFile(const std::string& filename, const std::string& contents)
{
    std::ofstream t("/" + filename);
    t << contents;
}

int OpenModel(std::string filename)
{
    std::cout << "Loading: " << filename << std::endl;

    uint32_t modelID = loaders.size();
    std::string contents = ReadFile("filename");
    
    std::cout << "Read " << std::endl;

    webifc::IfcLoader loader;
    loaders.push_back(loader);
    std::cout << "Loading " << std::endl;
    auto start = webifc::ms();
    loaders[modelID].LoadFile(contents);
    auto end = webifc::ms() - start;
    geomLoaders.push_back(webifc::IfcGeometryLoader(loaders[modelID]));

    std::cout << "Loaded " << loaders[modelID].GetNumLines() << " lines in " << end << " ms!" << std::endl;

    return modelID;
}

void CloseModel(uint32_t modelID)
{
    // TODO: use a list or something else...

    // overwrite old loaders, thereby destructing them
    loaders[modelID] = webifc::IfcLoader();
    // geomLoaders[modelID] = webifc::IfcGeometryLoader(loaders[modelID]);
}

std::vector<webifc::IfcFlatMesh> LoadAllGeometry(uint32_t modelID)
{
    webifc::IfcLoader& loader = loaders[modelID];
    webifc::IfcGeometryLoader& geomLoader = geomLoaders[modelID];

    std::vector<webifc::IfcFlatMesh> meshes;

    for (auto type : ifc2x4::IfcElements)
    {
        auto elements = loader.GetExpressIDsWithType(type);

        if (type == ifc2x4::IFCOPENINGELEMENT || type == ifc2x4::IFCSPACE || type == ifc2x4::IFCOPENINGSTANDARDCASE)
        {
            continue;
        }

        for (int i = 0; i < elements.size(); i++)
        {
            webifc::IfcFlatMesh mesh = geomLoader.GetFlatMesh(elements[i]);
            for (auto& geom : mesh.geometries)
            {
                auto& flatGeom = geomLoader.GetCachedGeometry(geom.geometryExpressID);
                flatGeom.GetVertexData();
            }   
            meshes.push_back(std::move(mesh));
        }
    }

    return meshes;
}

webifc::IfcGeometry GetGeometry(uint32_t modelID, uint32_t expressID)
{
    return geomLoaders[modelID].GetCachedGeometry(expressID);
}

void SetGeometryTransformation(uint32_t modelID, std::array<double, 16> m)
{
    glm::dmat4 transformation;
    glm::dvec4 v1(m[0], m[1], m[2], m[3]);
    glm::dvec4 v2(m[4], m[5], m[6], m[7]);
    glm::dvec4 v3(m[8], m[9], m[10], m[11]);
    glm::dvec4 v4(m[12], m[13], m[14], m[15]);

    transformation[0] = v1;
    transformation[1] = v2;
    transformation[2] = v3;
    transformation[3] = v4;

    geomLoaders[modelID].SetTransformation(transformation);
}

std::vector<uint32_t> GetLineIDsWithType(uint32_t modelID, uint32_t type)
{
    webifc::IfcLoader& loader = loaders[modelID];
    auto lineIDs = loader.GetLineIDsWithType(type);
    std::vector<uint32_t> expressIDs;
    for (auto lineID : lineIDs)
    {
        expressIDs.push_back(loader.GetLine(lineID).expressID);
    }
    return expressIDs;
}

std::vector<uint32_t> GetAllLines(uint32_t modelID)
{
    webifc::IfcLoader& loader = loaders[modelID];
    std::vector<uint32_t> expressIDs;
    auto numLines = loader.GetNumLines();
    for (int i = 0; i < numLines; i++)
    {
        expressIDs.push_back(loader.GetLine(i).expressID);
    }
    return expressIDs;
}

void ExportFileAsIFC(uint32_t modelID)
{
    webifc::IfcLoader& loader = loaders[modelID];
    std::string exportData = loader.DumpAsIFC();
    WriteFile("export.ifc", exportData);
    std::cout << "Exported" << std::endl;
}

void WriteSet(webifc::DynamicTape<TAPE_SIZE>& _tape, emscripten::val& val)
{
    _tape.push(webifc::IfcTokenType::SET_BEGIN);

    uint32_t size = val["length"].as<uint32_t>();
    int index = 0;
    while (true && index < size)
    {
        emscripten::val child = val[std::to_string(index)];
        if (child.isArray())
        {
            WriteSet(_tape, child);
        }
        else
        {
            webifc::IfcTokenType type = static_cast<webifc::IfcTokenType>(child.as<uint32_t>());
            _tape.push(type);
            switch(type)
            {
                case webifc::IfcTokenType::LINE_END:
                case webifc::IfcTokenType::UNKNOWN:
                case webifc::IfcTokenType::EMPTY:
                case webifc::IfcTokenType::SET_BEGIN:
                case webifc::IfcTokenType::SET_END:
                {
                    // ignore
                    break;
                }
                case webifc::IfcTokenType::STRING:
                case webifc::IfcTokenType::ENUM:
                case webifc::IfcTokenType::LABEL:
                {
                    child = val[++index];
                    std::string copy = child.as<std::string>();

                    uint8_t length = copy.size();
					_tape.push(length);
                    _tape.push((void*)copy.c_str(), copy.size());

                    break;
                }
                case webifc::IfcTokenType::REF:
                {
                    child = val[++index];
                    uint32_t val = child.as<uint32_t>();
					_tape.push(&val, sizeof(uint32_t));

                    break;
                }
                case webifc::IfcTokenType::REAL:
                {
                    child = val[++index];
                    double val = child.as<double>();
                    _tape.push(&val, sizeof(double));

                    break;
                }
                default:
                    break;
            }
        }

        index++;
    }

    _tape.push(webifc::IfcTokenType::SET_END);
}

void WriteLine(uint32_t modelID, uint32_t expressID, uint32_t type, emscripten::val parameters)
{
    webifc::IfcLoader& loader = loaders[modelID];
    auto& _tape = loader.GetTape();

    _tape.SetWriteAtEnd();

    // line ID
    _tape.push(webifc::IfcTokenType::REF);
    _tape.push(&expressID, sizeof(uint32_t));

    // line TYPE
    const char* ifcName = GetReadableNameFromTypeCode(type);
    _tape.push(webifc::IfcTokenType::LABEL);
    uint8_t length = strlen(ifcName);
    _tape.push(length);
    _tape.push((void*)ifcName, length);

    WriteSet(_tape, parameters);

    // end line
    _tape.push(webifc::IfcTokenType::LINE_END);
}

template<uint32_t N>
emscripten::val ReadValue(webifc::DynamicTape<N>& tape, webifc::IfcTokenType t)
{
    switch (t)
    {
    case webifc::IfcTokenType::STRING:
    case webifc::IfcTokenType::ENUM:
    {
        webifc::StringView view = tape.ReadStringView();
        std::string copy(view.data, view.len);

        return emscripten::val(copy);
    }
    case webifc::IfcTokenType::REAL:
    {
        double d = tape.template Read<double>();

        return emscripten::val(d);
    }
    default:
        // use undefined to signal val parse issue
        return emscripten::val::undefined();
    }
}

emscripten::val GetLine(uint32_t modelID, uint32_t expressID)
{
    webifc::IfcLoader& loader = loaders[modelID];

    auto& line = loader.GetLine(loader.ExpressIDToLineID(expressID));
    auto& _tape = loader.GetTape();

    loader.MoveToArgumentOffset(line, 0);

    std::stack<emscripten::val> valueStack;
    std::stack<int> valuePosition;

    auto arguments = emscripten::val::array();

    valueStack.push(arguments);
    valuePosition.push(0);

    bool endOfLine = false;
    while (!_tape.AtEnd() && !endOfLine)
    {
        webifc::IfcTokenType t = static_cast<webifc::IfcTokenType>(_tape.Read<char>());

        auto& topValue = valueStack.top();
        auto& topPosition = valuePosition.top();

        switch (t)
        {
        case webifc::IfcTokenType::LINE_END:
        {
            endOfLine = true;
            break;
        }
        case webifc::IfcTokenType::UNKNOWN:
        {
            auto obj = emscripten::val::object(); 
            obj.set("type", emscripten::val(static_cast<uint32_t>(webifc::IfcTokenType::UNKNOWN))); 
            topValue.set(topPosition++, obj);
            
            break;
        }
        case webifc::IfcTokenType::EMPTY:
        {
            topValue.set(topPosition++, emscripten::val::null());
            
            break;
        }
        case webifc::IfcTokenType::SET_BEGIN:
        {
            auto newValue = emscripten::val::array();

            valueStack.push(newValue);
            valuePosition.push(0);

            break;
        }
        case webifc::IfcTokenType::SET_END:
        {
            if (valueStack.size() == 1)
            {
                // this is a pop just before endline, so ignore
                endOfLine = true;
            }
            else
            {
                auto topCopy = valueStack.top();

                valueStack.pop();
                valuePosition.pop();
                auto& parent = valueStack.top();
                int& parentCount = valuePosition.top();
                parent.set(parentCount++, topCopy);
            }

            break;
        }
        case webifc::IfcTokenType::REF:
        {
            uint32_t ref = _tape.Read<uint32_t>();
            auto obj = emscripten::val::object(); 
            obj.set("type", emscripten::val(static_cast<uint32_t>(webifc::IfcTokenType::REF))); 
            obj.set("expressID", emscripten::val(ref)); 
            topValue.set(topPosition++, obj);

            break;
        }
        case webifc::IfcTokenType::LABEL:
        {
            // read label
            webifc::StringView view = _tape.ReadStringView();
            std::string copy(view.data, view.len);

            auto obj = emscripten::val::object(); 
            obj.set("type", emscripten::val(static_cast<uint32_t>(webifc::IfcTokenType::LABEL)));
            obj.set("label", emscripten::val(copy));

            // read set open
            _tape.Read<char>();
            
            // read value following label
            webifc::IfcTokenType t = static_cast<webifc::IfcTokenType>(_tape.Read<char>());
            obj.set("value", ReadValue(_tape, t));

            // read set close
            _tape.Read<char>();

            topValue.set(topPosition++, obj);

            break;
        }
        case webifc::IfcTokenType::STRING:
        case webifc::IfcTokenType::ENUM:
        case webifc::IfcTokenType::REAL:
        {
            topValue.set(topPosition++, ReadValue(_tape, t));

            break;
        }
        default:
            break;
        }
    }

    auto retVal = emscripten::val::object();
    retVal.set(emscripten::val("ID"), line.expressID);
    retVal.set(emscripten::val("type"), line.ifcType);
    retVal.set(emscripten::val("arguments"), arguments);

    return retVal;
}

extern "C" bool IsModelOpen(uint32_t modelID)
{
    return loaders[modelID].IsOpen();
}
    
EMSCRIPTEN_BINDINGS(my_module) {

    emscripten::class_<webifc::IfcGeometry>("IfcGeometry")
        .constructor<>()
        .function("GetVertexData", &webifc::IfcGeometry::GetVertexData)
        .function("GetVertexDataSize", &webifc::IfcGeometry::GetVertexDataSize)
        .function("GetIndexData", &webifc::IfcGeometry::GetIndexData)
        .function("GetIndexDataSize", &webifc::IfcGeometry::GetIndexDataSize)
        ;


    emscripten::value_object<glm::dvec4>("dvec4")
        .field("x", &glm::dvec4::x)
        .field("y", &glm::dvec4::y)
        .field("z", &glm::dvec4::z)
        .field("w", &glm::dvec4::w)
        ;

    emscripten::value_array<std::array<double, 16>>("array_double_16")
            .element(emscripten::index<0>())
            .element(emscripten::index<1>())
            .element(emscripten::index<2>())
            .element(emscripten::index<3>())
            .element(emscripten::index<4>())
            .element(emscripten::index<5>())
            .element(emscripten::index<6>())
            .element(emscripten::index<7>())
            .element(emscripten::index<8>())
            .element(emscripten::index<9>())
            .element(emscripten::index<10>())
            .element(emscripten::index<11>())
            .element(emscripten::index<12>())
            .element(emscripten::index<13>())
            .element(emscripten::index<14>())
            .element(emscripten::index<15>())
            ;

    emscripten::value_object<webifc::IfcPlacedGeometry>("IfcPlacedGeometry")
        .field("color", &webifc::IfcPlacedGeometry::color)
        .field("flatTransformation", &webifc::IfcPlacedGeometry::flatTransformation)
        .field("geometryExpressID", &webifc::IfcPlacedGeometry::geometryExpressID)
        ;

    emscripten::register_vector<webifc::IfcPlacedGeometry>("IfcPlacedGeometryVector");

    emscripten::value_object<webifc::IfcFlatMesh>("IfcFlatMesh")
        .field("geometries", &webifc::IfcFlatMesh::geometries)
        .field("expressID", &webifc::IfcFlatMesh::expressID)
        ;

    emscripten::register_vector<webifc::IfcFlatMesh>("IfcFlatMeshVector");
    emscripten::register_vector<uint32_t>("UintVector");

    emscripten::function("LoadAllGeometry", &LoadAllGeometry);
    emscripten::function("OpenModel", &OpenModel);
    emscripten::function("CloseModel", &CloseModel);
    emscripten::function("IsModelOpen", &IsModelOpen);
    emscripten::function("GetGeometry", &GetGeometry);
    emscripten::function("GetLine", &GetLine);
    emscripten::function("WriteLine", &WriteLine);
    emscripten::function("ExportFileAsIFC", &ExportFileAsIFC);
    emscripten::function("GetLineIDsWithType", &GetLineIDsWithType);
    emscripten::function("GetAllLines", &GetAllLines);
    emscripten::function("SetGeometryTransformation", &SetGeometryTransformation);
}