#include "RailingCommands.hpp"
#include "MigrationHelper.hpp"
#include "ExtendedElementCommands.hpp"
#include <string>

// Railings: read and change the reference line, its elevation and the segment height.
// The reference line lives in memo.coords (1-based) with one absolute elevation per vertex in
// memo.polyZCoords, as CreateRailings writes it. The height is the one of the default segment
// (railing.defSegment.height), which is what the DevKit's Element_Test changes (Do_Railing_Edit).

static bool IsRailing (const API_Element& elem)
{
#ifdef ServerMainVers_2600
    return elem.header.type.typeID == API_RailingID;
#else
    return elem.header.typeID == API_RailingID;
#endif
}

// Element ID, edited property values and classifications of one element onto another (a railing rebuilt on a new
// reference line). Each property is set on its own so that one refused value does not drop the others.
static void CopyIdentity (const API_Guid& from, const API_Guid& to)
{
    GS::Array<API_PropertyDefinition> definitions;
    if (ACAPI_Element_GetPropertyDefinitions (from, API_PropertyDefinitionFilter_All, definitions) == NoError) {
        GS::Array<API_PropertyDefinition> editable;
        for (const API_PropertyDefinition& definition : definitions) {
            if (definition.canValueBeEditable) {
                editable.Push (definition);
            }
        }
        GS::Array<API_Property> properties;
        if (ACAPI_Element_GetPropertyValues (from, editable, properties) == NoError) {
            for (const API_Property& property : properties) {
                if (property.isDefault || property.status != API_Property_HasValue) {
                    continue;
                }
                GS::Array<API_Property> one;
                one.Push (property);
                ACAPI_Element_SetProperties (to, one);
            }
        }
    }
    GS::Array<GS::Pair<API_Guid, API_Guid>> classifications;
    if (ACAPI_Element_GetClassificationItems (from, classifications) == NoError) {
        for (const auto& systemAndItem : classifications) {
            ACAPI_Element_AddClassificationItem (to, systemAndItem.second);
        }
    }
}

GetRailingsCommand::GetRailingsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String GetRailingsCommand::GetName () const
{
    return "GetRailings";
}

GS::Optional<GS::UniString> GetRailingsCommand::GetInputParametersSchema () const
{
    return R"({
    "type": "object",
    "properties": {
        "elements": {
            "$ref": "#/Elements"
        }
    },
    "additionalProperties": false,
    "required": [
        "elements"
    ]
})";
}

GS::Optional<GS::UniString> GetRailingsCommand::GetRawResponseSchema () const
{
    return R"({
    "type": "object",
    "properties": {
        "railings": {
            "type": "array",
            "items": {
                "type": "object",
                "description": "The railing, or an error for an element that is not a railing.",
                "properties": {
                    "floorIndex": { "type": "number" },
                    "bottomOffset": { "type": "number", "description": "Base offset of the railing relative to its floor." },
                    "height": { "type": "number", "description": "Height of the default segment." },
                    "polylineCoordinates": {
                        "type": "array",
                        "description": "The reference line, in drawing order.",
                        "items": { "$ref": "#/Coordinate2D" }
                    },
                    "polylineZCoordinates": {
                        "type": "array",
                        "description": "Absolute elevation of each reference line vertex.",
                        "items": { "type": "number" }
                    },
                    "error": { "$ref": "#/Error" }
                },
                "additionalProperties": false
            }
        }
    },
    "additionalProperties": false,
    "required": [
        "railings"
    ]
})";
}

GS::ObjectState GetRailingsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> elements;
    parameters.Get ("elements", elements);

    GS::ObjectState response;
    const auto& railings = response.AddList<GS::ObjectState> ("railings");

    for (const GS::ObjectState& item : elements) {
        const GS::ObjectState* elementId = item.Get ("elementId");
        if (elementId == nullptr) {
            railings (CreateErrorResponse (APIERR_BADPARS, "elementId is missing"));
            continue;
        }
        API_Element elem = {};
        elem.header.guid = GetGuidFromObjectState (*elementId);
        GSErrCode err = ACAPI_Element_Get (&elem);
        if (err != NoError || !IsRailing (elem)) {
            railings (CreateErrorResponse (err != NoError ? err : APIERR_BADID, "The element is not a Railing."));
            continue;
        }
        API_ElementMemo memo = {};
        const GS::OnExit guard ([&memo] () { ACAPI_DisposeElemMemoHdls (&memo); });
        err = ACAPI_Element_GetMemo (elem.header.guid, &memo, APIMemoMask_Polygon);
        if (err != NoError || memo.coords == nullptr) {
            railings (CreateErrorResponse (err != NoError ? err : APIERR_GENERAL, "Failed to read the railing reference line."));
            continue;
        }
        GS::ObjectState os;
        os.Add ("floorIndex", elem.header.floorInd);
        os.Add ("bottomOffset", elem.railing.bottomOffset);
        os.Add ("height", elem.railing.defSegment.height);
        const auto& coords = os.AddList<GS::ObjectState> ("polylineCoordinates");
        const auto& zs = os.AddList<double> ("polylineZCoordinates");
        const Int32 n = static_cast<Int32> (elem.railing.nVertices);
        for (Int32 i = 1; i <= n; ++i) {
            coords (Create2DCoordinateObjectState ((*memo.coords)[i]));
            zs (memo.polyZCoords != nullptr ? (*memo.polyZCoords)[i] : 0.0);
        }
        railings (os);
    }
    return response;
}

ModifyRailingsCommand::ModifyRailingsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String ModifyRailingsCommand::GetName () const
{
    return "ModifyRailings";
}

// The schema of upstream's ModifyRailings (#1793 and after: height on every segment, bottomOffset, floorIndex,
// visibility, pens, segments, nodes), with the fork's level and polylineCoordinates added to each item. Its height
// has the draft 4 exclusive minimum Archicad enforces (e63c13324); the fork's own "exclusiveMinimum": 0.0 let 0 and
// negative heights through (Archicad validates with JSON Schema draft 4, where a number there is ignored).
GS::Optional<GS::UniString> ModifyRailingsCommand::GetInputParametersSchema () const
{
    std::string schema = ModifyRailingsUpstreamCommand ().GetInputParametersSchema ().Get ().ToCStr (CC_UTF8).Get ();
    const std::string anchor = "\"elementId\": {";
    const size_t at = schema.find (anchor);
    if (at == std::string::npos) {
        return ModifyRailingsUpstreamCommand ().GetInputParametersSchema ();
    }
    schema.insert (at, R"("level": { "type": "number", "description": "Fork Imaginns: new absolute elevation of the reference line (all vertices)." },
                        "polylineCoordinates": {
                            "type": "array",
                            "description": "Fork Imaginns: new reference line. Archicad does not take a new reference line on an existing railing, so it is rebuilt with its settings and the old one is deleted; the response's elements give the new one. Without level, the vertices keep the elevation of the first current vertex.",
                            "items": { "$ref": "#/Coordinate2D" },
                            "minItems": 2
                        },
                        )");
    return GS::UniString (schema.c_str (), CC_UTF8);
}

GS::Optional<GS::UniString> ModifyRailingsCommand::GetRawResponseSchema () const
{
    return R"({
    "type": "object",
    "properties": {
        "executionResults": {
            "$ref": "#/ExecutionResults"
        },
        "elements": {
            "type": "array",
            "description": "The railing after the change, in the order of the request: the same element, or the new one when the reference line was replaced (Archicad does not take a new reference line on an existing railing, so it is rebuilt with all its settings and the old one is deleted).",
            "items": { "$ref": "#/ElementIdArrayItem" }
        }
    },
    "additionalProperties": false,
    "required": [
        "executionResults",
        "elements"
    ]
})";
}

// The fork's part of ModifyRailings: level and polylineCoordinates. The height and every other field go through
// upstream's code (ModifyRailingsUpstreamCommand), so withHeight is false when called from Execute.
static GS::ObjectState ExecuteOwnPart (const GS::Array<GS::ObjectState>& items, bool withHeight)
{

    GS::ObjectState response;
    const auto& executionResults = response.AddList<GS::ObjectState> ("executionResults");
    const auto& outElements = response.AddList<GS::ObjectState> ("elements");

    TAPIR_CallUndoableCommand ("ModifyRailings", [&] () -> GSErrCode {
        for (const GS::ObjectState& item : items) {
            const GS::ObjectState* elementId = item.Get ("elementId");
            if (elementId == nullptr) {
                executionResults (CreateFailedExecutionResult (APIERR_BADPARS, "elementId is missing"));
                outElements (GS::ObjectState ());
                continue;
            }
            API_Element elem = {};
            elem.header.guid = GetGuidFromObjectState (*elementId);
            GSErrCode err = ACAPI_Element_Get (&elem);
            if (err != NoError || !IsRailing (elem)) {
                executionResults (CreateFailedExecutionResult (err != NoError ? err : APIERR_BADID, "The element is not a Railing."));
                outElements (CreateElementIdObjectState (elem.header.guid));
                continue;
            }

            API_Element mask;
            ACAPI_ELEMENT_MASK_CLEAR (mask);
            double height = 0.0;
            if (withHeight && item.Get ("height", height)) {
                elem.railing.defSegment.height = height;
                ACAPI_ELEMENT_MASK_SET (mask, API_RailingType, defSegment.height);
            }

            GS::Array<GS::ObjectState> coordinates;
            double level = 0.0;
            const bool hasLine  = item.Get ("polylineCoordinates", coordinates);
            const bool hasLevel = item.Get ("level", level);

            API_ElementMemo memo = {};
            const GS::OnExit guard ([&memo] () { ACAPI_DisposeElemMemoHdls (&memo); });
            UInt64 memoMask = 0;
            if (hasLine || hasLevel) {
                err = ACAPI_Element_GetMemo (elem.header.guid, &memo, APIMemoMask_Polygon);
                if (err != NoError || memo.coords == nullptr) {
                    executionResults (CreateFailedExecutionResult (err != NoError ? err : APIERR_GENERAL, "Failed to read the railing reference line."));
                    continue;
                }
                const double currentZ = memo.polyZCoords != nullptr ? (*memo.polyZCoords)[1] : 0.0;
                const double z = hasLevel ? level : currentZ;
                if (hasLine) {
                    if (coordinates.GetSize () < 2) {
                        executionResults (CreateFailedExecutionResult (APIERR_BADPARS, "A railing needs at least 2 reference line points."));
                        continue;
                    }
                    const Int32 n = static_cast<Int32> (coordinates.GetSize ());
                    BMKillHandle (reinterpret_cast<GSHandle*> (&memo.coords));
                    BMKillHandle (reinterpret_cast<GSHandle*> (&memo.parcs));
                    BMKillHandle (reinterpret_cast<GSHandle*> (&memo.polyZCoords));
                    memo.coords = reinterpret_cast<API_Coord**> (BMAllocateHandle ((n + 1) * sizeof (API_Coord), ALLOCATE_CLEAR, 0));
                    memo.polyZCoords = reinterpret_cast<double**> (BMAllocateHandle ((n + 1) * sizeof (double), ALLOCATE_CLEAR, 0));
                    Int32 i = 0;
                    for (const GS::ObjectState& c : coordinates) {
                        (*memo.coords)[++i] = Get2DCoordinateFromObjectState (c);
                    }
                    elem.railing.nVertices = static_cast<UInt32> (n);
                    ACAPI_ELEMENT_MASK_SET (mask, API_RailingType, nVertices);
                }
                const Int32 n = static_cast<Int32> (elem.railing.nVertices);
                for (Int32 i = 0; i <= n; ++i) {
                    (*memo.polyZCoords)[i] = z;
                }
                memoMask = APIMemoMask_Polygon;
            }

            if (hasLine) {
                // Measured on 18/10/2026: ACAPI_Element_Change answers NoError and keeps the old reference line. The
                // railing is rebuilt from its own settings (defNode / defSegment, layer, floor) on the new line instead.
                const API_Guid oldGuid = elem.header.guid;
                elem.header.guid = APINULLGuid;
                err = ACAPI_Element_Create (&elem, &memo);
                if (err != NoError) {
                    executionResults (CreateFailedExecutionResult (err, "Failed to rebuild the railing on the new reference line."));
                    outElements (CreateElementIdObjectState (oldGuid));
                    continue;
                }
                // The rebuilt railing only carries the settings of the API_Element: its Element ID, property values and
                // classifications were lost (bench, 19/10/2026: GARDE_CORPS-014 came back as GARDE_CORPS-017). Copy them
                // from the old railing before it is deleted.
                CopyIdentity (oldGuid, elem.header.guid);
                GS::Array<API_Guid> toDelete;
                toDelete.Push (oldGuid);
                ACAPI_Element_Delete (toDelete);
                executionResults (CreateSuccessfulExecutionResult ());
                outElements (CreateElementIdObjectState (elem.header.guid));
                continue;
            }
            err = ACAPI_Element_Change (&elem, &mask, memoMask != 0 ? &memo : nullptr, memoMask, true);
            if (err != NoError) {
                executionResults (CreateFailedExecutionResult (err, "Failed to change the railing."));
                outElements (CreateElementIdObjectState (elem.header.guid));
                continue;
            }
            executionResults (CreateSuccessfulExecutionResult ());
            outElements (CreateElementIdObjectState (elem.header.guid));
        }
        return NoError;
    });

    return response;
}

// Each item goes first through upstream's ModifyRailings for its own fields, then through the fork's part for level
// and polylineCoordinates (a new line rebuilds the railing from its settings, the ones just changed included). An
// item with only the fork's fields skips upstream's code, which refuses an item without one of its fields.
GS::ObjectState ModifyRailingsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const
{
    GS::Array<GS::ObjectState> items;
    parameters.Get ("railingsWithDetails", items);

    GS::ObjectState response;
    const auto& executionResults = response.AddList<GS::ObjectState> ("executionResults");
    const auto& outElements = response.AddList<GS::ObjectState> ("elements");

    for (const GS::ObjectState& item : items) {
        const GS::ObjectState* elementId = item.Get ("elementId");
        GS::ObjectState element = elementId != nullptr ? CreateElementIdObjectState (GetGuidFromObjectState (*elementId)) : GS::ObjectState ();
        bool hasUpstreamFields = false;
        item.EnumerateFields ([&] (const GS::String& name) {
            if (name != "elementId" && name != "level" && name != "polylineCoordinates") {
                hasUpstreamFields = true;
            }
        });
        const bool hasOwnFields = item.Contains ("level") || item.Contains ("polylineCoordinates");

        GS::ObjectState result = CreateSuccessfulExecutionResult ();
        if (hasUpstreamFields || !hasOwnFields) {
            GS::ObjectState upstreamParameters;
            const auto& upstreamItems = upstreamParameters.AddList<GS::ObjectState> ("railingsWithDetails");
            upstreamItems (item);
            const GS::ObjectState upstreamResponse = ModifyRailingsUpstreamCommand ().Execute (upstreamParameters, processControl);
            GS::Array<GS::ObjectState> upstreamResults;
            if (upstreamResponse.Get ("executionResults", upstreamResults) && !upstreamResults.IsEmpty ()) {
                result = upstreamResults[0];
            } else {
                const GS::ObjectState* error = upstreamResponse.Get ("error");
                GSErrCode code = APIERR_GENERAL;
                GS::UniString message = "Failed to modify the railing.";
                if (error != nullptr) {
                    error->Get ("code", code);
                    error->Get ("message", message);
                }
                result = CreateFailedExecutionResult (code, message);
            }
        }
        bool success = false;
        result.Get ("success", success);
        if (success && hasOwnFields) {
            GS::Array<GS::ObjectState> ownItems;
            ownItems.Push (item);
            const GS::ObjectState ownResponse = ExecuteOwnPart (ownItems, false);
            GS::Array<GS::ObjectState> ownResults;
            GS::Array<GS::ObjectState> ownElements;
            ownResponse.Get ("executionResults", ownResults);
            ownResponse.Get ("elements", ownElements);
            result = !ownResults.IsEmpty () ? ownResults[0] : CreateFailedExecutionResult (APIERR_GENERAL, "Failed to change the railing.");
            if (!ownElements.IsEmpty ()) {
                element = ownElements[0];
            }
        }
        executionResults (result);
        outElements (element);
    }
    return response;
}
