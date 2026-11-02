#include "APIEnvir.h"
// Notes 3D is Windows only (GDI+, Win32 files): nothing of it is built on macOS.
#if defined (WINDOWS)

// Notes 3D Imaginns : dessin des traits sur la capture (GDI+).
#include "Notes3DRendu.hpp"

#include <windows.h>
// Tapir builds with WIN32_LEAN_AND_MEAN, which leaves out the COM declarations (IStream, PROPID) GDI+ needs
#include <objidl.h>
#include <propidl.h>
#include <algorithm>
namespace Gdiplus { using std::min; using std::max; }
#pragma warning (push)   // Tapir treats warnings as errors; the GDI+ headers shadow their own members (C4458)
#pragma warning (disable: 4458)
#include <gdiplus.h>
#pragma warning (pop)

namespace Rendu {

static ULONG_PTR jetonGdiplus = 0;

void Demarrer ()
{
	if (jetonGdiplus != 0)
		return;
	Gdiplus::GdiplusStartupInput entree;
	Gdiplus::GdiplusStartup (&jetonGdiplus, &entree, nullptr);
}

void Arreter ()
{
	if (jetonGdiplus != 0) {
		Gdiplus::GdiplusShutdown (jetonGdiplus);
		jetonGdiplus = 0;
	}
}

static bool ClsidPng (CLSID& clsid)
{
	UINT nombre = 0, taille = 0;
	if (Gdiplus::GetImageEncodersSize (&nombre, &taille) != Gdiplus::Ok || taille == 0)
		return false;
	std::vector<BYTE> tampon (taille);
	Gdiplus::ImageCodecInfo* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*> (tampon.data ());
	if (Gdiplus::GetImageEncoders (nombre, taille, codecs) != Gdiplus::Ok)
		return false;
	for (UINT i = 0; i < nombre; ++i) {
		if (wcscmp (codecs[i].MimeType, L"image/png") == 0) {
			clsid = codecs[i].Clsid;
			return true;
		}
	}
	return false;
}

bool Taille (const wchar_t* chemin, int& largeur, int& hauteur)
{
	Demarrer ();
	Gdiplus::Bitmap image (chemin);
	if (image.GetLastStatus () != Gdiplus::Ok)
		return false;
	largeur = (int) image.GetWidth ();
	hauteur = (int) image.GetHeight ();
	return largeur > 0 && hauteur > 0;
}

bool DessinerTraits (const wchar_t* source, const wchar_t* destination, const std::vector<Trait>& traits, std::string& erreur)
{
	Demarrer ();
	int largeur = 0, hauteur = 0;
	Gdiplus::Bitmap* resultat = nullptr;
	{
		// la capture est lue puis relachee avant l'ecriture (GDI+ garde le fichier ouvert tant que l'image vit)
		Gdiplus::Bitmap brute (source);
		if (brute.GetLastStatus () != Gdiplus::Ok) {
			erreur = "capture brute illisible";
			return false;
		}
		largeur = (int) brute.GetWidth ();
		hauteur = (int) brute.GetHeight ();
		resultat = new Gdiplus::Bitmap (largeur, hauteur, PixelFormat32bppARGB);
		Gdiplus::Graphics g (resultat);
		g.DrawImage (&brute, 0, 0, largeur, hauteur);
	}
	{
		Gdiplus::Graphics g (resultat);
		g.SetSmoothingMode (Gdiplus::SmoothingModeAntiAlias);
		for (const Trait& t : traits) {
			if (t.points.empty ())
				continue;
			const Gdiplus::Color couleur (255, t.r, t.g, t.b);
			const float ep = (float) (t.epaisseur < 1.0 ? 1.0 : t.epaisseur);
			if (t.points.size () == 1) {
				Gdiplus::SolidBrush pinceau (couleur);
				g.FillEllipse (&pinceau, (float) t.points[0].x - ep / 2, (float) t.points[0].y - ep / 2, ep, ep);
				continue;
			}
			Gdiplus::Pen stylo (couleur, ep);
			stylo.SetStartCap (Gdiplus::LineCapRound);
			stylo.SetEndCap (Gdiplus::LineCapRound);
			stylo.SetLineJoin (Gdiplus::LineJoinRound);
			std::vector<Gdiplus::PointF> pts;
			pts.reserve (t.points.size ());
			for (const Point& p : t.points)
				pts.push_back (Gdiplus::PointF ((float) p.x, (float) p.y));
			g.DrawLines (&stylo, pts.data (), (INT) pts.size ());
		}
	}
	CLSID png;
	if (!ClsidPng (png)) {
		delete resultat;
		erreur = "encodeur PNG introuvable";
		return false;
	}
	const Gdiplus::Status s = resultat->Save (destination, &png, nullptr);
	delete resultat;
	if (s != Gdiplus::Ok) {
		erreur = "ecriture de l'image annotee impossible";
		return false;
	}
	return true;
}

}

#endif
