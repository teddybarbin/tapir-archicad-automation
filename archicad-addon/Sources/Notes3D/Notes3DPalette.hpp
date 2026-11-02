// Notes 3D Imaginns : la palette (page HTML dans un navigateur DG).
#ifndef NOTES3D_PALETTE_HPP
#define NOTES3D_PALETTE_HPP

#include "APIEnvir.h"
#include "ACAPinc.h"
#include "DGModule.hpp"
#include "DGBrowser.hpp"

class PaletteNotes final : public DG::Palette, public DG::PanelObserver
{
public:
	virtual ~PaletteNotes ();

	static bool             Existe ();
	static PaletteNotes&    Instance ();   // la cree au besoin
	static void             Detruire ();
	static GSErrCode        EnregistrerRappel ();

	void Montrer ();
	void Cacher ();
	void ExecuterJS (const GS::UniString& code);

	// appele par les notifications du projet : la page relit la liste
	static void Rafraichir ();

private:
	enum { NavigateurId = 1 };
	DG::Browser navigateur;

	PaletteNotes ();
	void InitialiserPage ();
	void EnregistrerObjetJS ();
	void CocherMenu (bool coche);

	virtual void PanelResized (const DG::PanelResizeEvent& ev) override;
	virtual void PanelCloseRequested (const DG::PanelCloseRequestEvent& ev, bool* accepte) override;

	static GSErrCode __ACENV_CALL RappelPalette (Int32 id, API_PaletteMessageID message, GS::IntPtr param);
	static GS::Ref<PaletteNotes> instance;
};

#endif
