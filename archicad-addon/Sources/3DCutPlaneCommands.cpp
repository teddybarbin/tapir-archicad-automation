#include "GSUnID.hpp"
#include "3DCutPlaneCommands.hpp"
#include "MigrationHelper.hpp"

GS::String Set3DCutPlanesCommand::GetName () const
{
    return "Set3DCutPlanes";
}


// there is no 3DCutPlane scheme definition.
// I am not sure if it is needed, because it will not occur in other commands
Set3DCutPlanesCommand::Set3DCutPlanesCommand () :
    CommandBase (CommonSchema::NotUsed)
{}


static GSErrCode _Set3DCutPlanes (const GS::Array<GS::ObjectState>& cutPlanes)
{
    API_3DCutPlanesInfo cutInfo {};

    GSErrCode err = ACAPI_View_Get3DCuttingPlanes (&cutInfo);
    if (err == NoError) {
        if (cutInfo.shapes != nullptr)
            BMKillHandle ((GSHandle*) &(cutInfo.shapes));

        cutInfo.isCutPlanes = true;
        cutInfo.useCustom = false;
        cutInfo.nShapes = (short) cutPlanes.GetSize ();
        cutInfo.shapes = reinterpret_cast<API_3DCutShapeType**> (BMAllocateHandle (cutInfo.nShapes * sizeof (API_3DCutShapeType), ALLOCATE_CLEAR, 0));
        if (cutInfo.shapes != nullptr) {
            // it is possible to define further attributes like cutStatus and cutPen
            // you can look it up here:
            // https://graphisoft.github.io/archicad-api-devkit/struct_a_p_i__3_d_cut_shape_type.html
            // for now, we only pass coordinates
            if (!cutPlanes.IsEmpty ()) {
                for (int i = 0; i < cutInfo.nShapes; ++i) {
                    cutPlanes[i].Get ("pa", (*cutInfo.shapes)[i].pa);
                    cutPlanes[i].Get ("pb", (*cutInfo.shapes)[i].pb);
                    cutPlanes[i].Get ("pc", (*cutInfo.shapes)[i].pc);
                    cutPlanes[i].Get ("pd", (*cutInfo.shapes)[i].pd);
                    Int32 cutStatus = 0;
                    if (cutPlanes[i].Get ("cutStatus", cutStatus)) {
                        (*cutInfo.shapes)[i].cutStatus = static_cast<short> (cutStatus);
                    }
                }
            } else {
                return APIERR_BADPARS;;
            }
        } else {
            return APIERR_MEMFULL;
        }

        err = ACAPI_View_Change3DCuttingPlanes (&cutInfo);

        BMKillHandle ((GSHandle*) &(cutInfo.shapes));
    }
    return err;
}


GS::Optional<GS::UniString> Set3DCutPlanesCommand::GetInputParametersSchema () const
{
    return R"({
    "type": "object",
    "properties": {
        "cutPlanes": {
            "type": "array",
            "minItems": 1,
            "items": {
                "type": "object",
                "description": "Defines a 3D cut plane using the plane equation: pa*x + pb*y + pc*z + pd = 0",
                "properties": {
                    "pa": {
                        "type": "number",
                        "description": "Coefficient of x in the plane equation. The x coordinate of the normal vector of the plane."
                    },
                    "pb": {
                        "type": "number",
                        "description": "Coefficient of y in the plane equation. The y coordinate of the normal vector of the plane."
                    },
                    "pc": {
                        "type": "number",
                        "description": "Coefficient of z in the plane equation. The z coordinate of the normal vector of the plane."
                    },
                    "pd": {
                        "type": "number",
                        "description": "Constant term in the plane equation. The distance of the plane from the origin along the normal vector."
                    },
                    "cutStatus": {
                        "type": "integer",
                        "description": "Raw cutStatus of the shape, as Get3DCutPlanes returns it (copy it back to restore a cut)."
                    }
                },
                "required": [
                    "pa",
                    "pb",
                    "pc",
                    "pd"
                ],
                "additionalProperties": false
            }
        }
    },
    "additionalProperties": false
})";
}


GS::Optional<GS::UniString> Set3DCutPlanesCommand::GetRawResponseSchema () const
{
    return R"({
        "$ref": "#/ExecutionResult"
    })";
}

GS::ObjectState Set3DCutPlanesCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> cutPlanes;
    if (!parameters.Get ("cutPlanes", cutPlanes)) {
        return CreateFailedExecutionResult (APIERR_BADPARS, "Invalid parameter: cutPlanes.");
    }

    GSErrCode err = TAPIR_CallUndoableCommand ("Set3DCutPlanesCommand", [&]() -> GSErrCode {
        return _Set3DCutPlanes (cutPlanes);
    });

    return err == NoError
        ? CreateSuccessfulExecutionResult ()
        : CreateFailedExecutionResult (err, "Failed to set 3D cut planes.");
}

// Reads the current 3D cutting planes, raw, so that a cut set by the user can be saved and put back (19/10/2026: a
// script needed its own cut on the working project and had no way to restore the user's) and so that the meaning of
// pa..pd can be checked against a cut drawn in the UI (the DevKit documents them as a section line, the Set schema as
// plane coefficients).
Get3DCutPlanesCommand::Get3DCutPlanesCommand () :
    CommandBase (CommonSchema::NotUsed)
{
}

GS::String Get3DCutPlanesCommand::GetName () const
{
    return "Get3DCutPlanes";
}

GS::Optional<GS::UniString> Get3DCutPlanesCommand::GetRawResponseSchema () const
{
    return R"({
    "type": "object",
    "properties": {
        "isCutPlanes": { "type": "boolean" },
        "cutPlanes": {
            "type": "array",
            "items": {
                "type": "object",
                "properties": {
                    "pa": { "type": "number" },
                    "pb": { "type": "number" },
                    "pc": { "type": "number" },
                    "pd": { "type": "number" },
                    "cutStatus": { "type": "integer" },
                    "cutPen": { "type": "integer" }
                },
                "additionalProperties": false,
                "required": ["pa", "pb", "pc", "pd", "cutStatus", "cutPen"]
            }
        }
    },
    "additionalProperties": false,
    "required": ["isCutPlanes", "cutPlanes"]
})";
}

GS::ObjectState Get3DCutPlanesCommand::Execute (const GS::ObjectState& /*parameters*/, GS::ProcessControl& /*processControl*/) const
{
    API_3DCutPlanesInfo cutInfo {};
    GSErrCode err = ACAPI_View_Get3DCuttingPlanes (&cutInfo);
    if (err != NoError) {
        return CreateErrorResponse (err, "Failed to read the 3D cut planes.");
    }
    GS::ObjectState response ("isCutPlanes", cutInfo.isCutPlanes);
    const auto& list = response.AddList<GS::ObjectState> ("cutPlanes");
    if (cutInfo.shapes != nullptr) {
        for (short i = 0; i < cutInfo.nShapes; ++i) {
            const API_3DCutShapeType& sh = (*cutInfo.shapes)[i];
            GS::ObjectState os;
            os.Add ("pa", sh.pa);
            os.Add ("pb", sh.pb);
            os.Add ("pc", sh.pc);
            os.Add ("pd", sh.pd);
            os.Add ("cutStatus", static_cast<Int32> (sh.cutStatus));
            os.Add ("cutPen", static_cast<Int32> (sh.cutPen));
            list (os);
        }
        BMKillHandle ((GSHandle*) &(cutInfo.shapes));
    }
    return response;
}
