#include "APIEnvir.h"
// Notes 3D is Windows only (GDI+, Win32 files): nothing of it is built on macOS.
#if defined (WINDOWS)

// Notes 3D Imaginns : la palette.
#include "Notes3DPalette.hpp"
#include "Notes3DNotes.hpp"
#include "ResourceIds.hpp"

static const GS::Guid guidPalette ("{6B1D2C9E-4A57-4F0B-9E3C-1A2B3C4D5E6F}");

GS::Ref<PaletteNotes> PaletteNotes::instance;

static GS::UniString PageHtml ()
{
	GS::UniString page;
	GSHandle donnees = RSLoadResource ('FILE', ACAPI_GetOwnResModule (), ID_NOTES3D_PALETTE_HTML_FILE);
	if (donnees != nullptr) {
		const GSSize taille = BMhGetSize (donnees);
		page = GS::UniString (*donnees, (USize) taille, CC_UTF8);
		BMhKill (&donnees);
	}
	return page;
}

static std::string Chaine (GS::Ref<JS::Base> param)
{
	GS::Ref<JS::Value> v = GS::DynamicCast<JS::Value> (param);
	if (v != nullptr && v->GetType () == JS::Value::STRING)
		return Notes3D::VersUtf8 (v->GetString ());
	return std::string ();
}

static GS::Ref<JS::Base> VersJS (const std::string& s)
{
	return new JS::Value (Notes3D::DepuisUtf8 (s));
}

PaletteNotes::PaletteNotes () :
	DG::Palette (ACAPI_GetOwnResModule (), ID_NOTES3D_PALETTE, ACAPI_GetOwnResModule (), guidPalette),
	navigateur (GetReference (), NavigateurId)
{
	Attach (*this);
	BeginEventProcessing ();
	InitialiserPage ();
}

PaletteNotes::~PaletteNotes ()
{
	EndEventProcessing ();
}

bool PaletteNotes::Existe ()
{
	return instance != nullptr;
}

PaletteNotes& PaletteNotes::Instance ()
{
	if (instance == nullptr) {
		instance = new PaletteNotes ();
	}
	return *instance;
}

void PaletteNotes::Detruire ()
{
	instance = nullptr;
}

void PaletteNotes::Montrer ()
{
	DG::Palette::Show ();
	CocherMenu (true);
}

void PaletteNotes::Cacher ()
{
	DG::Palette::Hide ();
	CocherMenu (false);
}

void PaletteNotes::ExecuterJS (const GS::UniString& code)
{
	navigateur.ExecuteJS (code);
}

void PaletteNotes::Rafraichir ()
{
	if (Existe ())
		Instance ().ExecuterJS ("if (window.rafraichir) rafraichir ();");
}

void PaletteNotes::InitialiserPage ()
{
	EnregistrerObjetJS ();
	navigateur.LoadHTML (PageHtml ());
}

// Les fonctions que la page appelle (chacune rend une promesse cote JS). Elles prennent et rendent du texte JSON.
void PaletteNotes::EnregistrerObjetJS ()
{
	JS::Object* objet = new JS::Object ("NOTES3D");

	objet->AddItem (new JS::Function ("Lister", [] (GS::Ref<JS::Base>) {
		return VersJS (Notes3D::Lister ());
	}));
	objet->AddItem (new JS::Function ("Noter", [] (GS::Ref<JS::Base> p) {
		return VersJS (Notes3D::Noter (Chaine (p)));
	}));
	objet->AddItem (new JS::Function ("Image", [] (GS::Ref<JS::Base> p) {
		return VersJS (Notes3D::ImageDataUrl (Chaine (p)));
	}));
	objet->AddItem (new JS::Function ("Aller", [] (GS::Ref<JS::Base> p) {
		return VersJS (Notes3D::AllerA (Chaine (p)));
	}));
	objet->AddItem (new JS::Function ("Supprimer", [] (GS::Ref<JS::Base> p) {
		return VersJS (Notes3D::Supprimer (Chaine (p)));
	}));
	// une seule chaine JSON : {"id":..., "traits":[...]}
	objet->AddItem (new JS::Function ("Traits", [] (GS::Ref<JS::Base> p) {
		const std::string s = Chaine (p);
		const size_t sep = s.find ('\n');
		if (sep == std::string::npos)
			return VersJS (Notes3D::Erreur ("format attendu : id, saut de ligne, traits"));
		return VersJS (Notes3D::EnregistrerTraits (s.substr (0, sep), s.substr (sep + 1)));
	}));
	// journal de la page (tests) : %TEMP%\notes3d_palette.log
	objet->AddItem (new JS::Function ("Journal", [] (GS::Ref<JS::Base> p) {
		const std::string s = Chaine (p) + "\r\n";
		wchar_t tmp[MAX_PATH] = {};
		GetTempPathW (MAX_PATH, tmp);
		std::wstring chemin = std::wstring (tmp) + L"notes3d_palette.log";
		HANDLE h = CreateFileW (chemin.c_str (), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (h != INVALID_HANDLE_VALUE) {
			DWORD n = 0;
			WriteFile (h, s.data (), (DWORD) s.size (), &n, nullptr);
			CloseHandle (h);
		}
		return VersJS (std::string ("ok"));
	}));

	navigateur.RegisterAsynchJSObject (objet);
}

void PaletteNotes::CocherMenu (bool coche)
{
	API_MenuItemRef ref {};
	GSFlags drapeaux {};
	ref.menuResID = ID_NOTES3D_MENU_PALETTE;
	ref.itemIndex = 1;
	ACAPI_MenuItem_GetMenuItemFlags (&ref, &drapeaux);
	if (coche)
		drapeaux |= API_MenuItemChecked;
	else
		drapeaux &= ~API_MenuItemChecked;
	ACAPI_MenuItem_SetMenuItemFlags (&ref, &drapeaux);
}

void PaletteNotes::PanelResized (const DG::PanelResizeEvent& ev)
{
	BeginMoveResizeItems ();
	navigateur.Resize (ev.GetHorizontalChange (), ev.GetVerticalChange ());
	EndMoveResizeItems ();
}

void PaletteNotes::PanelCloseRequested (const DG::PanelCloseRequestEvent&, bool* accepte)
{
	Cacher ();
	*accepte = true;
}

GSErrCode __ACENV_CALL PaletteNotes::RappelPalette (Int32, API_PaletteMessageID message, GS::IntPtr param)
{
	switch (message) {
		case APIPalMsg_OpenPalette:
			Instance ().Montrer ();
			break;
		case APIPalMsg_ClosePalette:
			if (Existe ()) Instance ().Cacher ();
			break;
		case APIPalMsg_HidePalette_Begin:
			if (Existe () && Instance ().IsVisible ()) Instance ().Hide ();
			break;
		case APIPalMsg_HidePalette_End:
			if (Existe () && !Instance ().IsVisible ()) Instance ().Show ();
			break;
		case APIPalMsg_DisableItems_Begin:
			if (Existe () && Instance ().IsVisible ()) Instance ().DisableItems ();
			break;
		case APIPalMsg_DisableItems_End:
			if (Existe () && Instance ().IsVisible ()) Instance ().EnableItems ();
			break;
		case APIPalMsg_IsPaletteVisible:
			*(reinterpret_cast<bool*> (param)) = Existe () && Instance ().IsVisible ();
			break;
		default:
			break;
	}
	return NoError;
}

GSErrCode PaletteNotes::EnregistrerRappel ()
{
	return ACAPI_RegisterModelessWindow (
		GS::CalculateHashValue (guidPalette),
		RappelPalette,
		API_PalEnabled_FloorPlan + API_PalEnabled_Section + API_PalEnabled_Elevation +
		API_PalEnabled_InteriorElevation + API_PalEnabled_3D + API_PalEnabled_Detail +
		API_PalEnabled_Worksheet + API_PalEnabled_Layout + API_PalEnabled_DocumentFrom3D,
		GSGuid2APIGuid (guidPalette));
}

#endif
