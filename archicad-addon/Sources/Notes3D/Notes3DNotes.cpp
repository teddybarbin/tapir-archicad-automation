#include "APIEnvir.h"
// Notes 3D is Windows only (GDI+, Win32 files): nothing of it is built on macOS.
#if defined (WINDOWS)

// Notes 3D Imaginns : magasin des notes et vues.
#include "Notes3DNotes.hpp"
#include "Notes3DRendu.hpp"
#include "CommandBase.hpp"

#define RAPIDJSON_HAS_STDSTRING 1
#include "RapidJSON/document.h"
#include "RapidJSON/prettywriter.h"
#include "RapidJSON/stringbuffer.h"
#include "RapidJSON/writer.h"

#include <shlobj.h>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace rapidjson;

namespace Notes3D {

// ---------------------------------------------------------------------------------------------------------------------
// Outils
// ---------------------------------------------------------------------------------------------------------------------

std::string VersUtf8 (const GS::UniString& s)
{
	return std::string (s.ToCStr (CC_UTF8).Get ());
}

GS::UniString DepuisUtf8 (const std::string& s)
{
	return GS::UniString (s.c_str (), (USize) s.size (), CC_UTF8);
}

// copie UTF-16 a terminaison nulle, pour les fonctions Win32 / GDI+
static std::wstring W (const GS::UniString& s)
{
	return std::wstring (reinterpret_cast<const wchar_t*> (s.ToUStr ().Get ()));
}

static std::string Ecrire (const Value& v, bool joli = false)
{
	StringBuffer sb;
	if (joli) {
		PrettyWriter<StringBuffer> w (sb);
		w.SetIndent ('\t', 1);
		v.Accept (w);
	} else {
		Writer<StringBuffer> w (sb);
		v.Accept (w);
	}
	return std::string (sb.GetString (), sb.GetSize ());
}

std::string Erreur (const std::string& message)
{
	Document d (kObjectType);
	auto& a = d.GetAllocator ();
	d.AddMember ("ok", false, a);
	d.AddMember ("erreur", Value (message.c_str (), a), a);
	return Ecrire (d);
}

static bool LireFichier (const GS::UniString& chemin, std::string& contenu)
{
	contenu.clear ();
	HANDLE h = CreateFileW ((LPCWSTR) chemin.ToUStr ().Get (), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
							nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	LARGE_INTEGER taille {};
	GetFileSizeEx (h, &taille);
	contenu.resize ((size_t) taille.QuadPart);
	DWORD lus = 0;
	const BOOL ok = contenu.empty () ? TRUE : ReadFile (h, &contenu[0], (DWORD) contenu.size (), &lus, nullptr);
	CloseHandle (h);
	if (!ok || lus != (DWORD) contenu.size ()) {
		contenu.clear ();
		return false;
	}
	return true;
}

// Ecrit a cote puis remplace : notes.json n'est jamais laisse a moitie ecrit.
static bool EcrireFichier (const GS::UniString& chemin, const std::string& contenu)
{
	GS::UniString tmp = chemin + ".tmp";
	HANDLE h = CreateFileW ((LPCWSTR) tmp.ToUStr ().Get (), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	DWORD ecrits = 0;
	const BOOL ok = WriteFile (h, contenu.data (), (DWORD) contenu.size (), &ecrits, nullptr);
	FlushFileBuffers (h);
	CloseHandle (h);
	if (!ok || ecrits != (DWORD) contenu.size ())
		return false;
	return MoveFileExW ((LPCWSTR) tmp.ToUStr ().Get (), (LPCWSTR) chemin.ToUStr ().Get (), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

static bool Existe (const GS::UniString& chemin)
{
	return GetFileAttributesW ((LPCWSTR) chemin.ToUStr ().Get ()) != INVALID_FILE_ATTRIBUTES;
}

static void CreerDossier (const GS::UniString& chemin)
{
	SHCreateDirectoryExW (nullptr, (LPCWSTR) chemin.ToUStr ().Get (), nullptr);
}

static GS::UniString NomSansExtension (const GS::UniString& chemin)
{
	UIndex barre = chemin.FindLast ('\\');
	GS::UniString nom = (barre == MaxUIndex) ? chemin : chemin.GetSubstring (barre + 1, chemin.GetLength () - barre - 1);
	UIndex point = nom.FindLast ('.');
	if (point != MaxUIndex)
		nom = nom.GetSubstring (0, point);
	return nom;
}

static std::string Base64 (const std::string& octets)
{
	static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string r;
	r.reserve ((octets.size () + 2) / 3 * 4);
	size_t i = 0;
	for (; i + 2 < octets.size (); i += 3) {
		const unsigned n = ((unsigned char) octets[i] << 16) | ((unsigned char) octets[i + 1] << 8) | (unsigned char) octets[i + 2];
		r += t[(n >> 18) & 63]; r += t[(n >> 12) & 63]; r += t[(n >> 6) & 63]; r += t[n & 63];
	}
	if (i + 1 == octets.size ()) {
		const unsigned n = ((unsigned char) octets[i] << 16);
		r += t[(n >> 18) & 63]; r += t[(n >> 12) & 63]; r += "==";
	} else if (i + 2 == octets.size ()) {
		const unsigned n = ((unsigned char) octets[i] << 16) | ((unsigned char) octets[i + 1] << 8);
		r += t[(n >> 18) & 63]; r += t[(n >> 12) & 63]; r += t[(n >> 6) & 63]; r += '=';
	}
	return r;
}

static std::string Horodatage (bool pourId)
{
	SYSTEMTIME st;
	GetLocalTime (&st);
	char b[64];
	if (pourId)
		snprintf (b, sizeof (b), "%04d%02d%02d-%02d%02d%02d-%03d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	else
		snprintf (b, sizeof (b), "%04d-%02d-%02dT%02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
	return b;
}

static std::string GuidTexte (const API_Guid& g)
{
	return VersUtf8 (APIGuidToString (g));
}

static API_Guid GuidDepuis (const std::string& s)
{
	return APIGuidFromString (s.c_str ());
}

// ---------------------------------------------------------------------------------------------------------------------
// Projet et dossier des notes
// ---------------------------------------------------------------------------------------------------------------------

struct Projet {
	GS::UniString cheminPln;   // vide pour un projet sans titre
	GS::UniString nom;
	GS::UniString dossier;     // ...\notes-3d
};

static Projet ProjetCourant ()
{
	Projet p;
	API_ProjectInfo pi;
	if (ACAPI_ProjectOperation_Project (&pi) == NoError) {
		if (pi.projectName != nullptr)
			p.nom = *pi.projectName;
		if (!pi.untitled && !pi.teamwork && pi.location != nullptr) {
			pi.location->ToPath (&p.cheminPln);
		}
	}
	if (!p.cheminPln.IsEmpty ()) {
		UIndex barre = p.cheminPln.FindLast ('\\');
		if (barre != MaxUIndex)
			p.dossier = p.cheminPln.GetSubstring (0, barre) + "\\notes-3d";
		if (p.nom.IsEmpty ())
			p.nom = NomSansExtension (p.cheminPln);
	}
	if (p.dossier.IsEmpty ()) {
		// projet sans titre ou Teamwork : les notes vont dans Documents\Notes 3D Imaginns\<nom du projet>
		wchar_t docs[MAX_PATH] = {};
		SHGetFolderPathW (nullptr, CSIDL_PERSONAL, nullptr, 0, docs);
		GS::UniString nom = p.nom.IsEmpty () ? GS::UniString ("sans-titre") : p.nom;
		p.dossier = GS::UniString ((const GS::UniChar::Layout*) docs) + "\\Notes 3D Imaginns\\" + nom + "\\notes-3d";
	}
	return p;
}

GS::UniString DossierNotes ()
{
	Projet p = ProjetCourant ();
	CreerDossier (p.dossier);
	return p.dossier;
}

static GS::UniString Fichier (const GS::UniString& dossier, const std::string& nom)
{
	return dossier + "\\" + DepuisUtf8 (nom);
}

static void ChargerMagasin (const GS::UniString& dossier, Document& d)
{
	std::string texte;
	if (LireFichier (dossier + "\\notes.json", texte) && !texte.empty ()) {
		d.Parse (texte.c_str (), texte.size ());
		if (!d.HasParseError () && d.IsObject () && d.HasMember ("notes") && d["notes"].IsArray ())
			return;
		// fichier abime : il est mis de cote, jamais ecrase
		GS::UniString abime = dossier + "\\notes.json.illisible-" + DepuisUtf8 (Horodatage (true));
		GS::UniString actuel = dossier + "\\notes.json";
		MoveFileExW ((LPCWSTR) actuel.ToUStr ().Get (), (LPCWSTR) abime.ToUStr ().Get (), 0);
	}
	d.SetObject ();
	auto& a = d.GetAllocator ();
	d.AddMember ("format", "notes-3d-imaginns", a);
	d.AddMember ("version", 1, a);
	d.AddMember ("notes", Value (kArrayType), a);
}

static bool EnregistrerMagasin (const GS::UniString& dossier, const Document& d)
{
	CreerDossier (dossier);
	return EcrireFichier (dossier + "\\notes.json", Ecrire (d, true));
}

static Value* TrouverNote (Document& d, const std::string& id, SizeType* indice = nullptr)
{
	Value& notes = d["notes"];
	for (SizeType i = 0; i < notes.Size (); ++i) {
		if (notes[i].IsObject () && notes[i].HasMember ("id") && notes[i]["id"].IsString () && id == notes[i]["id"].GetString ()) {
			if (indice) *indice = i;
			return &notes[i];
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------------------------------------------------
// Vue courante : fenetre, cadrage 2D, camera 3D
// ---------------------------------------------------------------------------------------------------------------------

struct TypeFenetre { API_WindowTypeID id; const char* nom; };
static const TypeFenetre typesFenetre[] = {
	{ APIWind_FloorPlanID, "plan" },
	{ APIWind_SectionID, "coupe" },
	{ APIWind_DetailID, "detail" },
	{ APIWind_3DModelID, "3D" },
	{ APIWind_LayoutID, "mise en page" },
	{ APIWind_DrawingID, "dessin" },
	{ APIWind_MasterLayoutID, "modele de mise en page" },
	{ APIWind_ElevationID, "facade" },
	{ APIWind_InteriorElevationID, "elevation interieure" },
	{ APIWind_WorksheetID, "feuille de travail" },
	{ APIWind_DocumentFrom3DID, "document 3D" },
};

static const char* NomType (API_WindowTypeID id)
{
	for (const TypeFenetre& t : typesFenetre)
		if (t.id == id) return t.nom;
	return "autre";
}

static void AjouterTableau (Value& o, const char* cle, const double* v, int n, Document::AllocatorType& a)
{
	Value t (kArrayType);
	for (int i = 0; i < n; ++i) t.PushBack (v[i], a);
	o.AddMember (StringRef (cle), t, a);
}

static void AjouterPoint3 (Value& o, const char* cle, double x, double y, double z, Document::AllocatorType& a)
{
	Value p (kObjectType);
	p.AddMember ("x", x, a); p.AddMember ("y", y, a); p.AddMember ("z", z, a);
	o.AddMember (StringRef (cle), p, a);
}

static double Nombre (const Value& o, const char* cle, double defaut = 0.0)
{
	return (o.IsObject () && o.HasMember (cle) && o[cle].IsNumber ()) ? o[cle].GetDouble () : defaut;
}

// La camera 3D, au format de Get3DProjectionSettings de Tapir pour la perspective (relisible et rejouable par l'API).
static GSErrCode CameraVersJson (Value& cam, Document::AllocatorType& a)
{
	API_3DProjectionInfo pi {};
	GSErrCode err = ACAPI_View_Get3DProjectionSets (&pi);
	if (err != NoError)
		return err;
	cam.SetObject ();
	cam.AddMember ("isPersp", pi.isPersp, a);
	if (pi.camGuid != APINULLGuid)
		cam.AddMember ("cameraElementGuid", Value (GuidTexte (pi.camGuid).c_str (), a), a);
	if (pi.isPersp) {
		const API_PerspPars& p = pi.u.persp;
		Value persp (kObjectType);
		AjouterPoint3 (persp, "cameraPosition", p.pos.x, p.pos.y, p.cameraZ, a);
		AjouterPoint3 (persp, "targetPosition", p.target.x, p.target.y, p.targetZ, a);
		persp.AddMember ("distance", p.distance, a);
		persp.AddMember ("azimuth", p.azimuth, a);
		persp.AddMember ("viewCone", p.viewCone, a);
		persp.AddMember ("rollAngle", p.rollAngle, a);
		persp.AddMember ("isTwoPointPersp", p.isTwoPointPersp, a);
		cam.AddMember ("perspective", persp, a);
	} else {
		const API_AxonoPars& x = pi.u.axono;
		Value axo (kObjectType);
		axo.AddMember ("azimuth", x.azimuth, a);
		axo.AddMember ("projMod", (int) x.projMod, a);
		AjouterTableau (axo, "transformationMatrix", x.tranmat.tmx, 12, a);
		AjouterTableau (axo, "inverseMatrix", x.invtranmat.tmx, 12, a);
		cam.AddMember ("axonometric", axo, a);
	}
	API_3DWindowInfo wi {};
	if (ACAPI_View_Get3DWindowSets (&wi) == NoError) {
		Value f (kObjectType);
		f.AddMember ("hSize", (int) wi.hSize, a);
		f.AddMember ("vSize", (int) wi.vSize, a);
		f.AddMember ("zoomScaleX", wi.zoomScaleX, a);
		f.AddMember ("zoomScaleY", wi.zoomScaleY, a);
		f.AddMember ("zoomDispX", wi.zoomDispX, a);
		f.AddMember ("zoomDispY", wi.zoomDispY, a);
		cam.AddMember ("fenetre3D", f, a);
	}
	return NoError;
}

static GSErrCode VueVersJson (const API_WindowInfo& fen, Value& vue, Document::AllocatorType& a)
{
	vue.SetObject ();
	Value f (kObjectType);
	f.AddMember ("type", StringRef (NomType (fen.typeID)), a);
	f.AddMember ("typeId", (int) fen.typeID, a);
	f.AddMember ("titre", Value (VersUtf8 (GS::UniString (fen.title)).c_str (), a), a);
	f.AddMember ("nom", Value (VersUtf8 (GS::UniString (fen.name)).c_str (), a), a);
	f.AddMember ("reference", Value (VersUtf8 (GS::UniString (fen.ref)).c_str (), a), a);
	f.AddMember ("databaseGuid", Value (GuidTexte (fen.databaseUnId.elemSetId).c_str (), a), a);
	if (fen.linkedElement != APINULLGuid)
		f.AddMember ("elementLie", Value (GuidTexte (fen.linkedElement).c_str (), a), a);
	vue.AddMember ("fenetre", f, a);

	if (fen.typeID == APIWind_3DModelID) {
		Value cam;
		GSErrCode err = CameraVersJson (cam, a);
		if (err != NoError)
			return err;
		vue.AddMember ("camera", cam, a);
	} else {
		if (fen.typeID == APIWind_FloorPlanID) {
			API_StoryInfo si {};
			if (ACAPI_ProjectSetting_GetStorySettings (&si) == NoError) {
				Value e (kObjectType);
				e.AddMember ("index", (int) si.actStory, a);
				if (si.data != nullptr) {
					const short n = si.lastStory - si.firstStory + 1;
					for (short i = 0; i < n; ++i) {
						const API_StoryType& s = (*si.data)[i];
						if (s.index == si.actStory) {
							e.AddMember ("nom", Value (VersUtf8 (GS::UniString (s.uName)).c_str (), a), a);
							e.AddMember ("niveau", s.level, a);
						}
					}
					BMKillHandle (reinterpret_cast<GSHandle*> (&si.data));
				}
				vue.AddMember ("etage", e, a);
			}
		}
		API_Box boite {};
		API_Tranmat tm {};
		if (ACAPI_View_GetZoom (&boite, &tm) == NoError) {
			Value z (kObjectType);
			z.AddMember ("xMin", boite.xMin, a);
			z.AddMember ("yMin", boite.yMin, a);
			z.AddMember ("xMax", boite.xMax, a);
			z.AddMember ("yMax", boite.yMax, a);
			AjouterTableau (z, "tranmat", tm.tmx, 12, a);
			vue.AddMember ("zoom2D", z, a);
		}
	}
	return NoError;
}

std::string CameraCourante ()
{
	API_WindowInfo fen {};
	GSErrCode err = ACAPI_Window_GetCurrentWindow (&fen);
	if (err != NoError)
		return Erreur ("fenetre courante illisible");
	Document d (kObjectType);
	auto& a = d.GetAllocator ();
	Value vue;
	err = VueVersJson (fen, vue, a);
	if (err != NoError)
		return Erreur ("vue illisible");
	d.AddMember ("ok", true, a);
	d.AddMember ("vue", vue, a);
	return Ecrire (d);
}

static GSErrCode AppliquerCamera (const Value& cam)
{
	API_3DProjectionInfo pi {};
	GSErrCode err = ACAPI_View_Get3DProjectionSets (&pi);
	if (err != NoError)
		return err;
	const bool persp = cam.HasMember ("isPersp") && cam["isPersp"].IsBool () && cam["isPersp"].GetBool ();
	const API_SunAngleSettings soleil = pi.isPersp ? pi.u.persp.sunAngSets : pi.u.axono.sunAngSets;
	pi.camGuid = APINULLGuid;
	pi.actCamSet = APINULLGuid;
	if (persp) {
		if (!cam.HasMember ("perspective") || !cam["perspective"].IsObject ())
			return APIERR_BADPARS;
		const Value& p = cam["perspective"];
		if (!p.HasMember ("cameraPosition") || !p.HasMember ("targetPosition"))
			return APIERR_BADPARS;
		const Value& c = p["cameraPosition"];
		const Value& t = p["targetPosition"];
		pi.isPersp = true;
		API_PerspPars& pp = pi.u.persp;
		pp = API_PerspPars {};
		pp.sunAngSets = soleil;
		pp.pos.x = Nombre (c, "x"); pp.pos.y = Nombre (c, "y"); pp.cameraZ = Nombre (c, "z");
		pp.target.x = Nombre (t, "x"); pp.target.y = Nombre (t, "y"); pp.targetZ = Nombre (t, "z");
		pp.distance = Nombre (p, "distance");
		pp.azimuth = Nombre (p, "azimuth");
		pp.viewCone = Nombre (p, "viewCone");
		pp.rollAngle = Nombre (p, "rollAngle");
		pp.isTwoPointPersp = p.HasMember ("isTwoPointPersp") && p["isTwoPointPersp"].IsBool () && p["isTwoPointPersp"].GetBool ();
	} else {
		if (!cam.HasMember ("axonometric") || !cam["axonometric"].IsObject ())
			return APIERR_BADPARS;
		const Value& x = cam["axonometric"];
		if (!x.HasMember ("transformationMatrix") || !x["transformationMatrix"].IsArray () || x["transformationMatrix"].Size () != 12)
			return APIERR_BADPARS;
		pi.isPersp = false;
		API_AxonoPars& ax = pi.u.axono;
		ax = API_AxonoPars {};
		ax.sunAngSets = soleil;
		ax.azimuth = Nombre (x, "azimuth");
		ax.projMod = (short) Nombre (x, "projMod");
		for (int i = 0; i < 12; ++i)
			ax.tranmat.tmx[i] = x["transformationMatrix"][i].GetDouble ();
		if (x.HasMember ("inverseMatrix") && x["inverseMatrix"].IsArray () && x["inverseMatrix"].Size () == 12)
			for (int i = 0; i < 12; ++i)
				ax.invtranmat.tmx[i] = x["inverseMatrix"][i].GetDouble ();
	}
	err = TAPIR_CallUndoableCommand ("Notes 3D : remettre la vue", [&] () -> GSErrCode {
		return ACAPI_View_Change3DProjectionSets (&pi, nullptr);
	});
	if (err != NoError)
		return err;
	if (cam.HasMember ("fenetre3D") && cam["fenetre3D"].IsObject ()) {
		const Value& f = cam["fenetre3D"];
		API_3DWindowInfo wi {};
		if (ACAPI_View_Get3DWindowSets (&wi) == NoError && Nombre (f, "zoomScaleX") > 0 && Nombre (f, "zoomScaleY") > 0) {
			wi.setWindowSize = false;
			wi.setZoom = true;
			wi.zoomScaleX = Nombre (f, "zoomScaleX");
			wi.zoomScaleY = Nombre (f, "zoomScaleY");
			wi.zoomDispX = Nombre (f, "zoomDispX");
			wi.zoomDispY = Nombre (f, "zoomDispY");
			ACAPI_View_Change3DWindowSets (&wi);
		}
	}
	return NoError;
}

static GSErrCode OuvrirFenetre (const Value& vue)
{
	if (!vue.HasMember ("fenetre") || !vue["fenetre"].IsObject ())
		return APIERR_BADPARS;
	const Value& f = vue["fenetre"];
	API_WindowInfo voulue {};
	voulue.typeID = (API_WindowTypeID) (int) Nombre (f, "typeId", (double) APIWind_3DModelID);
	if (f.HasMember ("databaseGuid") && f["databaseGuid"].IsString ())
		voulue.databaseUnId.elemSetId = GuidDepuis (f["databaseGuid"].GetString ());

	API_WindowInfo courante {};
	ACAPI_Window_GetCurrentWindow (&courante);
	const bool deja = courante.typeID == voulue.typeID &&
		(voulue.typeID == APIWind_3DModelID || voulue.typeID == APIWind_FloorPlanID || courante.databaseUnId.elemSetId == voulue.databaseUnId.elemSetId);
	if (!deja) {
		GSErrCode err = ACAPI_Window_ChangeWindow (&voulue);
		if (err != NoError)
			return err;
	}
	return NoError;
}

static GSErrCode AppliquerVue (const Value& vue)
{
	GSErrCode err = OuvrirFenetre (vue);
	if (err != NoError)
		return err;
	const int type = (int) Nombre (vue["fenetre"], "typeId");
	if (type == APIWind_3DModelID) {
		if (!vue.HasMember ("camera") || !vue["camera"].IsObject ())
			return APIERR_BADPARS;
		err = AppliquerCamera (vue["camera"]);
		if (err != NoError)
			return err;
		bool regenerer = false;
		ACAPI_View_Rebuild (&regenerer);
		return NoError;
	}
	if (type == APIWind_FloorPlanID && vue.HasMember ("etage") && vue["etage"].IsObject ()) {
		API_StoryInfo si {};
		short voulu = (short) Nombre (vue["etage"], "index");
		if (ACAPI_ProjectSetting_GetStorySettings (&si) == NoError) {
			const short actuel = si.actStory;
			BMKillHandle (reinterpret_cast<GSHandle*> (&si.data));
			if (actuel != voulu) {
				API_StoryCmdType cmd {};
				cmd.action = APIStory_GoTo;
				cmd.index = voulu;
				ACAPI_ProjectSetting_ChangeStorySettings (&cmd);
			}
		}
	}
	if (vue.HasMember ("zoom2D") && vue["zoom2D"].IsObject ()) {
		const Value& z = vue["zoom2D"];
		API_Box boite { Nombre (z, "xMin"), Nombre (z, "yMin"), Nombre (z, "xMax"), Nombre (z, "yMax") };
		API_Tranmat tm {};
		bool avecTm = false;
		if (z.HasMember ("tranmat") && z["tranmat"].IsArray () && z["tranmat"].Size () == 12) {
			for (int i = 0; i < 12; ++i) tm.tmx[i] = z["tranmat"][i].GetDouble ();
			avecTm = true;
		}
		err = ACAPI_View_Zoom (&boite, nullptr, avecTm ? &tm : nullptr);
		if (err != NoError)
			return err;
	}
	return NoError;
}

// ---------------------------------------------------------------------------------------------------------------------
// Capture de la fenetre courante
// ---------------------------------------------------------------------------------------------------------------------

static GSErrCode Capturer (const API_WindowInfo& fen, const GS::UniString& chemin)
{
	IO::Location lieu (chemin);
	API_FileSavePars fsp {};
	fsp.fileTypeID = APIFType_PNGFile;
	fsp.file = &lieu;
	API_SavePars_Picture pp {};
	pp.colorDepth = APIColorDepth_TC24;
	pp.dithered = false;
	pp.view2D = fen.typeID != APIWind_3DModelID;
	pp.crop = true;    // le cadrage visible, pas tout le dessin
	return ACAPI_ProjectOperation_Save (&fsp, &pp);
}

// ---------------------------------------------------------------------------------------------------------------------
// Operations
// ---------------------------------------------------------------------------------------------------------------------

std::string Lister ()
{
	Projet p = ProjetCourant ();
	Document d;
	ChargerMagasin (p.dossier, d);
	Document r (kObjectType);
	auto& a = r.GetAllocator ();
	r.AddMember ("ok", true, a);
	r.AddMember ("dossier", Value (VersUtf8 (p.dossier).c_str (), a), a);
	r.AddMember ("projet", Value (VersUtf8 (p.cheminPln).c_str (), a), a);
	r.AddMember ("nomProjet", Value (VersUtf8 (p.nom).c_str (), a), a);
	Value notes (d["notes"], a);
	r.AddMember ("notes", notes, a);
	return Ecrire (r);
}

std::string Noter (const std::string& texteUtf8)
{
	API_WindowInfo fen {};
	if (ACAPI_Window_GetCurrentWindow (&fen) != NoError)
		return Erreur ("aucune fenetre de projet ouverte");
	if (fen.typeID == APIWind_MasterLayoutID)
		return Erreur ("un modele de mise en page ne se capture pas : ouvre une mise en page qui l'utilise");

	Projet p = ProjetCourant ();
	CreerDossier (p.dossier);
	Document d;
	ChargerMagasin (p.dossier, d);
	auto& a = d.GetAllocator ();

	std::string id = Horodatage (true);
	while (TrouverNote (d, id) != nullptr || Existe (Fichier (p.dossier, id + ".png"))) {
		Sleep (2);
		id = Horodatage (true);
	}

	Value note (kObjectType);
	note.AddMember ("id", Value (id.c_str (), a), a);
	note.AddMember ("texte", Value (texteUtf8.c_str (), a), a);
	note.AddMember ("date", Value (Horodatage (false).c_str (), a), a);
	note.AddMember ("projet", Value (VersUtf8 (p.cheminPln).c_str (), a), a);
	note.AddMember ("nomProjet", Value (VersUtf8 (p.nom).c_str (), a), a);

	Value vue;
	if (VueVersJson (fen, vue, a) != NoError)
		return Erreur ("lecture de la vue impossible");
	note.AddMember ("vue", vue, a);

	const std::string nomImage = id + ".png";
	GS::UniString cheminImage = Fichier (p.dossier, nomImage);
	const GSErrCode errCapture = Capturer (fen, cheminImage);
	int largeur = 0, hauteur = 0;
	if (errCapture == NoError && Rendu::Taille (W (cheminImage).c_str (), largeur, hauteur)) {
		note.AddMember ("image", Value (nomImage.c_str (), a), a);
		note.AddMember ("largeur", largeur, a);
		note.AddMember ("hauteur", hauteur, a);
	} else {
		// la note est gardee sans image : le texte et la vue suffisent a la retrouver
		note.AddMember ("image", Value (kNullType), a);
		note.AddMember ("erreurCapture", errCapture, a);
	}
	note.AddMember ("imageAnnotee", Value (kNullType), a);
	note.AddMember ("traits", Value (kArrayType), a);

	Value copie (note, a);
	d["notes"].PushBack (note, a);
	if (!EnregistrerMagasin (p.dossier, d))
		return Erreur ("ecriture de notes.json impossible dans " + VersUtf8 (p.dossier));

	Document r (kObjectType);
	auto& ra = r.GetAllocator ();
	r.AddMember ("ok", true, ra);
	r.AddMember ("dossier", Value (VersUtf8 (p.dossier).c_str (), ra), ra);
	r.AddMember ("note", Value (copie, ra), ra);
	return Ecrire (r);
}

std::string AllerA (const std::string& id)
{
	Projet p = ProjetCourant ();
	Document d;
	ChargerMagasin (p.dossier, d);
	Value* note = TrouverNote (d, id);
	if (note == nullptr)
		return Erreur ("note introuvable : " + id);
	if (!note->HasMember ("vue") || !(*note)["vue"].IsObject ())
		return Erreur ("la note n'a pas de vue");
	const GSErrCode err = AppliquerVue ((*note)["vue"]);
	if (err != NoError)
		return Erreur ("la vue n'a pas pu etre remise (code " + std::to_string (err) + ")");

	API_WindowInfo fen {};
	ACAPI_Window_GetCurrentWindow (&fen);
	Document r (kObjectType);
	auto& a = r.GetAllocator ();
	r.AddMember ("ok", true, a);
	r.AddMember ("id", Value (id.c_str (), a), a);
	Value relue;
	if (VueVersJson (fen, relue, a) == NoError)
		r.AddMember ("vueRelue", relue, a);
	return Ecrire (r);
}

static void Deplacer (const GS::UniString& de, const GS::UniString& vers)
{
	if (Existe (de))
		MoveFileExW ((LPCWSTR) de.ToUStr ().Get (), (LPCWSTR) vers.ToUStr ().Get (), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
}

std::string Supprimer (const std::string& id)
{
	Projet p = ProjetCourant ();
	Document d;
	ChargerMagasin (p.dossier, d);
	SizeType i = 0;
	Value* note = TrouverNote (d, id, &i);
	if (note == nullptr)
		return Erreur ("note introuvable : " + id);

	// rien n'est efface : la note et ses images vont dans notes-3d\supprimees (supprimees.json garde l'entree)
	GS::UniString corbeille = p.dossier + "\\supprimees";
	CreerDossier (corbeille);
	Document c;
	ChargerMagasin (corbeille, c);
	auto& ca = c.GetAllocator ();
	Value copie (*note, ca);
	copie.AddMember ("supprimeeLe", Value (Horodatage (false).c_str (), ca), ca);
	c["notes"].PushBack (copie, ca);
	if (!EcrireFichier (corbeille + "\\supprimees.json", Ecrire (c, true)))
		return Erreur ("ecriture de supprimees.json impossible");
	Deplacer (Fichier (p.dossier, id + ".png"), Fichier (corbeille, id + ".png"));
	Deplacer (Fichier (p.dossier, id + "_annot.png"), Fichier (corbeille, id + "_annot.png"));

	d["notes"].Erase (d["notes"].Begin () + i);
	if (!EnregistrerMagasin (p.dossier, d))
		return Erreur ("ecriture de notes.json impossible");
	Document r (kObjectType);
	r.AddMember ("ok", true, r.GetAllocator ());
	return Ecrire (r);
}

static bool CouleurHex (const char* s, unsigned char& r, unsigned char& g, unsigned char& b)
{
	unsigned v = 0;
	if (s == nullptr || s[0] != '#' || strlen (s) != 7 || sscanf (s + 1, "%6x", &v) != 1)
		return false;
	r = (unsigned char) (v >> 16); g = (unsigned char) (v >> 8); b = (unsigned char) v;
	return true;
}

std::string EnregistrerTraits (const std::string& id, const std::string& traitsJson)
{
	Document entree;
	entree.Parse (traitsJson.c_str (), traitsJson.size ());
	if (entree.HasParseError () || !entree.IsArray ())
		return Erreur ("traits illisibles : une liste est attendue");

	Projet p = ProjetCourant ();
	Document d;
	ChargerMagasin (p.dossier, d);
	auto& a = d.GetAllocator ();
	Value* note = TrouverNote (d, id);
	if (note == nullptr)
		return Erreur ("note introuvable : " + id);

	Value traits (kArrayType);
	std::vector<Rendu::Trait> aDessiner;
	for (const Value& t : entree.GetArray ()) {
		if (!t.IsObject () || !t.HasMember ("points") || !t["points"].IsArray ())
			return Erreur ("trait sans points");
		Rendu::Trait rt;
		const char* coul = (t.HasMember ("couleur") && t["couleur"].IsString ()) ? t["couleur"].GetString () : "#ff1744";
		if (!CouleurHex (coul, rt.r, rt.g, rt.b))
			return Erreur (std::string ("couleur invalide : ") + coul);
		rt.epaisseur = Nombre (t, "epaisseur", 6.0);
		Value pts (kArrayType);
		for (const Value& pt : t["points"].GetArray ()) {
			Rendu::Point rp { Nombre (pt, "x"), Nombre (pt, "y") };
			rt.points.push_back (rp);
			Value o (kObjectType);
			o.AddMember ("x", std::round (rp.x * 10.0) / 10.0, a);
			o.AddMember ("y", std::round (rp.y * 10.0) / 10.0, a);
			pts.PushBack (o, a);
		}
		Value vt (kObjectType);
		vt.AddMember ("couleur", Value (coul, a), a);
		vt.AddMember ("epaisseur", rt.epaisseur, a);
		vt.AddMember ("points", pts, a);
		traits.PushBack (vt, a);
		aDessiner.push_back (rt);
	}

	GS::UniString brute = Fichier (p.dossier, id + ".png");
	GS::UniString annotee = Fichier (p.dossier, id + "_annot.png");
	(*note).RemoveMember ("traits");
	(*note).AddMember ("traits", traits, a);
	(*note).RemoveMember ("imageAnnotee");
	if (aDessiner.empty ()) {
		DeleteFileW (W (annotee).c_str ());   // image derivee des traits, refaite a la demande
		(*note).AddMember ("imageAnnotee", Value (kNullType), a);
	} else {
		if (!Existe (brute))
			return Erreur ("capture brute absente : traits non dessines");
		std::string err;
		if (!Rendu::DessinerTraits (W (brute).c_str (), W (annotee).c_str (), aDessiner, err))
			return Erreur (err);
		const std::string nom = id + "_annot.png";
		(*note).AddMember ("imageAnnotee", Value (nom.c_str (), a), a);
	}
	if (!EnregistrerMagasin (p.dossier, d))
		return Erreur ("ecriture de notes.json impossible");

	Document r (kObjectType);
	auto& ra = r.GetAllocator ();
	r.AddMember ("ok", true, ra);
	r.AddMember ("note", Value (*note, ra), ra);
	return Ecrire (r);
}

std::string ImageDataUrl (const std::string& id)
{
	Projet p = ProjetCourant ();
	std::string octets;
	if (!LireFichier (Fichier (p.dossier, id + ".png"), octets) || octets.empty ())
		return "";
	return "data:image/png;base64," + Base64 (octets);
}

void Demarrer () { Rendu::Demarrer (); }
void Arreter () { Rendu::Arreter (); }

}

#endif
