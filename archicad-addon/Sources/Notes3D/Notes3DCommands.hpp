#pragma once

#include "CommandBase.hpp"

// Notes 3D Imaginns (fork Imaginns, 01/11/2026): the notes Teddy takes in the Notes 3D palette, on any view (3D, plan,
// section, elevation, detail, worksheet, layout), each with its text, the camera or 2D frame of the view, a capture and
// the strokes drawn on it. Stored next to the .pln in <folder of the .pln>\notes-3d\ (notes.json and PNGs). The palette
// and these commands go through the same functions (Notes3DNotes.cpp): what the API reads is what Teddy sees.

class GetNotes3DCommand : public CommandBase
{
public:
    GetNotes3DCommand ();
    virtual GS::String GetName () const override;
    virtual GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    virtual GS::ObjectState Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const override;
};

class GoToNote3DCommand : public CommandBase
{
public:
    GoToNote3DCommand ();
    virtual GS::String GetName () const override;
    virtual GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    virtual GS::ObjectState Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const override;
};

class AddNote3DCommand : public CommandBase
{
public:
    AddNote3DCommand ();
    virtual GS::String GetName () const override;
    virtual GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    virtual GS::ObjectState Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const override;
};

class AnnotateNote3DCommand : public CommandBase
{
public:
    AnnotateNote3DCommand ();
    virtual GS::String GetName () const override;
    virtual GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    virtual GS::ObjectState Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const override;
};

class DeleteNote3DCommand : public CommandBase
{
public:
    DeleteNote3DCommand ();
    virtual GS::String GetName () const override;
    virtual GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    virtual GS::ObjectState Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const override;
};

class GetCurrentViewNotes3DCommand : public CommandBase
{
public:
    GetCurrentViewNotes3DCommand ();
    virtual GS::String GetName () const override;
    virtual GS::ObjectState Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const override;
};

class ShowNotes3DPaletteCommand : public CommandBase
{
public:
    ShowNotes3DPaletteCommand ();
    virtual GS::String GetName () const override;
    virtual GS::ObjectState Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const override;
};

class SimulateNotes3DPaletteCommand : public CommandBase
{
public:
    SimulateNotes3DPaletteCommand ();
    virtual GS::String GetName () const override;
    virtual GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    virtual GS::ObjectState Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const override;
};

// Menus, palette and project events of Notes 3D, called from AddOnMain.
GSErrCode Notes3D_RegisterMenus ();
GSErrCode Notes3D_Initialize ();
void Notes3D_OnProjectEvent (API_NotifyEventID notifID);
void Notes3D_FreeData ();
