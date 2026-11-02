#include "APIEnvir.h"
// Notes 3D is Windows only (GDI+, Win32 files): nothing of it is built on macOS.
#if defined (WINDOWS)

#include "Notes3DCommands.hpp"
#include "Notes3DNotes.hpp"
#include "Notes3DPalette.hpp"
#include "ResourceIds.hpp"

#include "ObjectStateJSONConversion.hpp"

#include <cstdio>

namespace {

// Notes3DNotes answers {"ok":true,...} or {"ok":false,"erreur":"..."} in UTF-8 JSON; the commands answer the Tapir way:
// the object itself on success, {"error": {code, message}} on failure.
GS::ObjectState Answer (const std::string& json)
{
    GS::ObjectState os;
    if (JSON::ConvertToObjectState (Notes3D::DepuisUtf8 (json), os) != NoError) {
        return CreateErrorResponse (APIERR_GENERAL, "Notes 3D: unreadable internal answer.");
    }
    bool ok = false;
    if (!os.Get ("ok", ok) || !ok) {
        GS::UniString message;
        os.Get ("erreur", message);
        return CreateErrorResponse (APIERR_GENERAL, "Notes 3D: " + message);
    }
    return os;
}

// A list parameter written back as JSON text.
std::string ListAsJson (const GS::ObjectState& parameters, const char* field)
{
    GS::Array<GS::ObjectState> list;
    if (!parameters.Get (field, list)) {
        return "";
    }
    GS::ObjectState envelope;
    const auto& items = envelope.AddList<GS::ObjectState> ("v");
    for (const GS::ObjectState& item : list) {
        items (item);
    }
    GS::UniString text;
    if (JSON::CreateFromObjectState (envelope, text) != NoError) {
        return "";
    }
    const std::string s = Notes3D::VersUtf8 (text);
    const size_t begin = s.find ('[');
    const size_t end = s.rfind (']');
    if (begin == std::string::npos || end == std::string::npos || end < begin) {
        return "";
    }
    return s.substr (begin, end - begin + 1);
}

std::string Text (const GS::ObjectState& parameters, const char* field)
{
    GS::UniString value;
    return parameters.Get (field, value) ? Notes3D::VersUtf8 (value) : std::string ();
}

// A safe JavaScript string literal from UTF-8.
GS::UniString JsString (const std::string& s)
{
    std::string r = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { r += '\\'; r += (char) c; }
        else if (c == '\n') r += "\\n";
        else if (c == '\r') r += "\\r";
        else if (c < 0x20) { char b[8]; snprintf (b, sizeof (b), "\\u%04x", c); r += b; }
        else r += (char) c;
    }
    r += "\"";
    return Notes3D::DepuisUtf8 (r);
}

GS::ObjectState MissingId ()
{
    return CreateErrorResponse (APIERR_BADPARS, "Missing parameter: id.");
}

const char* IdSchema = R"({
    "type": "object",
    "properties": {
        "id": { "type": "string", "description": "Identifier of the note, as GetNotes3D lists it." }
    },
    "additionalProperties": false,
    "required": ["id"]
})";

// A note added, annotated or deleted through the API changes notes.json behind the palette's back: the palette
// re-reads the list (bench, 02/11/2026: a note deleted by DeleteNote3D was still listed). Whatever the answer.
GS::ObjectState RefreshingPalette (const GS::ObjectState& answer)
{
    if (PaletteNotes::Existe ()) {
        PaletteNotes::Instance ().ExecuterJS ("if (window.rafraichir) rafraichir ();");
    }
    return answer;
}

} // namespace

GetNotes3DCommand::GetNotes3DCommand () : CommandBase (CommonSchema::NotUsed) {}
GS::String GetNotes3DCommand::GetName () const { return "GetNotes3D"; }

GS::Optional<GS::UniString> GetNotes3DCommand::GetRawResponseSchema () const
{
    return R"({
    "type": "object",
    "properties": {
        "dossier": { "type": "string", "description": "Folder of the notes: <folder of the .pln>\\notes-3d (Documents\\Notes 3D Imaginns\\<project> for an untitled or Teamwork project)." },
        "projet": { "type": "string", "description": "Path of the .pln, empty for an untitled project." },
        "nomProjet": { "type": "string" },
        "notes": {
            "type": "array",
            "description": "The notes of notes.json, oldest first: id, texte, date, vue (fenetre, camera for a 3D view, etage and zoom2D for a 2D view), image, largeur, hauteur, imageAnnotee, traits.",
            "items": { "type": "object" }
        }
    },
    "required": ["dossier", "projet", "notes"]
})";
}

GS::ObjectState GetNotes3DCommand::Execute (const GS::ObjectState& /*parameters*/, GS::ProcessControl& /*processControl*/) const
{
    return Answer (Notes3D::Lister ());
}

GoToNote3DCommand::GoToNote3DCommand () : CommandBase (CommonSchema::NotUsed) {}
GS::String GoToNote3DCommand::GetName () const { return "GoToNote3D"; }
GS::Optional<GS::UniString> GoToNote3DCommand::GetInputParametersSchema () const { return IdSchema; }

GS::ObjectState GoToNote3DCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    const std::string id = Text (parameters, "id");
    if (id.empty ()) {
        return MissingId ();
    }
    return Answer (Notes3D::AllerA (id));
}

AddNote3DCommand::AddNote3DCommand () : CommandBase (CommonSchema::NotUsed) {}
GS::String AddNote3DCommand::GetName () const { return "AddNote3D"; }

GS::Optional<GS::UniString> AddNote3DCommand::GetInputParametersSchema () const
{
    return R"({
    "type": "object",
    "properties": {
        "text": { "type": "string", "description": "Text of the note." }
    },
    "additionalProperties": false,
    "required": ["text"]
})";
}

GS::ObjectState AddNote3DCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::UniString text;
    if (!parameters.Get ("text", text)) {
        return CreateErrorResponse (APIERR_BADPARS, "Missing parameter: text.");
    }
    return RefreshingPalette (Answer (Notes3D::Noter (Notes3D::VersUtf8 (text))));
}

AnnotateNote3DCommand::AnnotateNote3DCommand () : CommandBase (CommonSchema::NotUsed) {}
GS::String AnnotateNote3DCommand::GetName () const { return "AnnotateNote3D"; }

GS::Optional<GS::UniString> AnnotateNote3DCommand::GetInputParametersSchema () const
{
    return R"({
    "type": "object",
    "properties": {
        "id": { "type": "string", "description": "Identifier of the note." },
        "strokes": {
            "type": "array",
            "description": "All the strokes of the note, replacing the previous ones; an empty list removes the annotated image. Points are in pixels of the capture.",
            "items": {
                "type": "object",
                "properties": {
                    "color": { "type": "string", "description": "#rrggbb, red #ff1744 by default." },
                    "width": { "type": "number", "description": "Width in pixels of the capture, 6 by default." },
                    "points": {
                        "type": "array",
                        "items": {
                            "type": "object",
                            "properties": { "x": { "type": "number" }, "y": { "type": "number" } },
                            "required": ["x", "y"]
                        }
                    }
                },
                "required": ["points"]
            }
        }
    },
    "additionalProperties": false,
    "required": ["id", "strokes"]
})";
}

GS::ObjectState AnnotateNote3DCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    const std::string id = Text (parameters, "id");
    if (id.empty ()) {
        return MissingId ();
    }
    GS::Array<GS::ObjectState> strokes;
    if (!parameters.Get ("strokes", strokes)) {
        return CreateErrorResponse (APIERR_BADPARS, "Missing parameter: strokes.");
    }
    // the store keeps the palette's own keys (couleur, epaisseur, points)
    GS::ObjectState converted;
    const auto& list = converted.AddList<GS::ObjectState> ("traits");
    for (const GS::ObjectState& stroke : strokes) {
        GS::ObjectState trait;
        GS::UniString color;
        if (stroke.Get ("color", color)) {
            trait.Add ("couleur", color);
        }
        double width = 0.0;
        if (stroke.Get ("width", width)) {
            trait.Add ("epaisseur", width);
        }
        GS::Array<GS::ObjectState> points;
        stroke.Get ("points", points);
        trait.Add ("points", points);
        list (trait);
    }
    std::string json = ListAsJson (converted, "traits");
    if (json.empty ()) {
        json = "[]";
    }
    return RefreshingPalette (Answer (Notes3D::EnregistrerTraits (id, json)));
}

DeleteNote3DCommand::DeleteNote3DCommand () : CommandBase (CommonSchema::NotUsed) {}
GS::String DeleteNote3DCommand::GetName () const { return "DeleteNote3D"; }
GS::Optional<GS::UniString> DeleteNote3DCommand::GetInputParametersSchema () const { return IdSchema; }

GS::ObjectState DeleteNote3DCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    const std::string id = Text (parameters, "id");
    if (id.empty ()) {
        return MissingId ();
    }
    return RefreshingPalette (Answer (Notes3D::Supprimer (id)));
}

GetCurrentViewNotes3DCommand::GetCurrentViewNotes3DCommand () : CommandBase (CommonSchema::NotUsed) {}
GS::String GetCurrentViewNotes3DCommand::GetName () const { return "GetCurrentViewNotes3D"; }

GS::ObjectState GetCurrentViewNotes3DCommand::Execute (const GS::ObjectState& /*parameters*/, GS::ProcessControl& /*processControl*/) const
{
    return Answer (Notes3D::CameraCourante ());
}

ShowNotes3DPaletteCommand::ShowNotes3DPaletteCommand () : CommandBase (CommonSchema::NotUsed) {}
GS::String ShowNotes3DPaletteCommand::GetName () const { return "ShowNotes3DPalette"; }

GS::ObjectState ShowNotes3DPaletteCommand::Execute (const GS::ObjectState& /*parameters*/, GS::ProcessControl& /*processControl*/) const
{
    PaletteNotes::Instance ().Montrer ();
    return CreateSuccessfulExecutionResult ();
}

SimulateNotes3DPaletteCommand::SimulateNotes3DPaletteCommand () : CommandBase (CommonSchema::NotUsed) {}
GS::String SimulateNotes3DPaletteCommand::GetName () const { return "SimulateNotes3DPalette"; }

GS::Optional<GS::UniString> SimulateNotes3DPaletteCommand::GetInputParametersSchema () const
{
    return R"({
    "type": "object",
    "properties": {
        "action": { "type": "string", "enum": ["noter", "tracer", "aller", "etat"], "description": "What the page does as Teddy would: noter (type text, then Enter), tracer (draw a stroke on the note), aller (double-click the note), etat (write the page state to %TEMP%\\notes3d_palette.log)." },
        "texte": { "type": "string" },
        "id": { "type": "string" },
        "points": { "type": "array", "items": { "type": "object" } }
    },
    "additionalProperties": false,
    "required": ["action"]
})";
}

// For tests only: plays in the page what Teddy would do. The page answers asynchronously.
GS::ObjectState SimulateNotes3DPaletteCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    const std::string action = Text (parameters, "action");
    GS::UniString code;
    if (action == "noter") {
        code = "simulerNote (" + JsString (Text (parameters, "texte")) + ");";
    } else if (action == "tracer") {
        code = "simulerTrait (" + JsString (Text (parameters, "id")) + ", " + JsString (ListAsJson (parameters, "points")) + ");";
    } else if (action == "aller") {
        code = "simulerDoubleClic (" + JsString (Text (parameters, "id")) + ");";
    } else if (action == "etat") {
        code = "journalEtat ();";
    } else {
        return CreateErrorResponse (APIERR_BADPARS, "Unknown action (noter, tracer, aller, etat).");
    }
    PaletteNotes& palette = PaletteNotes::Instance ();
    palette.Montrer ();
    palette.ExecuterJS (code);
    return CreateSuccessfulExecutionResult ();
}

// --- menus, palette and project events -------------------------------------------------------------------------------

static GSErrCode Notes3DMenuHandler (const API_MenuParams* params)
{
    switch (params->menuItemRef.menuResID) {
        case ID_NOTES3D_MENU_PALETTE:
            if (PaletteNotes::Existe () && PaletteNotes::Instance ().IsVisible ()) {
                PaletteNotes::Instance ().Cacher ();
            } else {
                PaletteNotes::Instance ().Montrer ();
            }
            break;
        case ID_NOTES3D_MENU_NOTER: {
            // opens the palette with the cursor in the text field: Teddy types the note and presses Enter
            PaletteNotes& palette = PaletteNotes::Instance ();
            palette.Montrer ();
            palette.ExecuterJS ("if (window.focusTexte) focusTexte ();");
            break;
        }
        default:
            break;
    }
    return NoError;
}

GSErrCode Notes3D_RegisterMenus ()
{
    GSErrCode err = ACAPI_MenuItem_RegisterMenu (ID_NOTES3D_MENU_PALETTE, 0, MenuCode_UserDef, MenuFlag_Default);
    err |= ACAPI_MenuItem_RegisterMenu (ID_NOTES3D_MENU_NOTER, 0, MenuCode_UserDef, MenuFlag_Default);
    return err;
}

GSErrCode Notes3D_Initialize ()
{
    Notes3D::Demarrer ();
    GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (ID_NOTES3D_MENU_PALETTE, Notes3DMenuHandler);
    err |= ACAPI_MenuItem_InstallMenuHandler (ID_NOTES3D_MENU_NOTER, Notes3DMenuHandler);
    err |= PaletteNotes::EnregistrerRappel ();
    return err;
}

void Notes3D_OnProjectEvent (API_NotifyEventID notifID)
{
    switch (notifID) {
        case APINotify_Quit:
            PaletteNotes::Detruire ();
            break;
        case APINotify_New:
        case APINotify_NewAndReset:
        case APINotify_Open:
        case APINotify_Close:
        case APINotify_Save:
            PaletteNotes::Rafraichir ();
            break;
        default:
            break;
    }
}

void Notes3D_FreeData ()
{
    PaletteNotes::Detruire ();
    Notes3D::Arreter ();
}

#endif
