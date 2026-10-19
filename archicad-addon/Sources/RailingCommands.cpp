#include "RailingCommands.hpp"
#include "MigrationHelper.hpp"

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

GS::Optional<GS::UniString> ModifyRailingsCommand::GetInputParametersSchema () const
{
    return R"({
    "type": "object",
    "properties": {
        "railingsWithDetails": {
            "type": "array",
            "items": {
                "type": "object",
                "properties": {
                    "elementId": { "$ref": "#/ElementId" },
                    "height": { "type": "number", "exclusiveMinimum": 0.0, "description": "New height of the railing segments." },
                    "level": { "type": "number", "description": "New absolute elevation of the reference line (all vertices)." },
                    "polylineCoordinates": {
                        "type": "array",
                        "description": "New reference line. Without level, the vertices keep the elevation of the first current vertex.",
                        "items": { "$ref": "#/Coordinate2D" },
                        "minItems": 2
                    }
                },
                "additionalProperties": false,
                "required": [
                    "elementId"
                ]
            }
        }
    },
    "additionalProperties": false,
    "required": [
        "railingsWithDetails"
    ]
})";
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

GS::ObjectState ModifyRailingsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> items;
    parameters.Get ("railingsWithDetails", items);

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
            if (item.Get ("height", height)) {
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
