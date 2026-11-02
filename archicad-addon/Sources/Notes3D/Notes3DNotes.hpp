// Notes 3D Imaginns : le magasin des notes (notes.json + captures) et la camera 3D.
// Tout ce qui ecrit sur le disque ou touche la camera passe par ici : la palette, les commandes JSON et les tests
// empruntent les memes fonctions.
#ifndef NOTES3D_NOTES_HPP
#define NOTES3D_NOTES_HPP

#include "APIEnvir.h"
#include "ACAPinc.h"

#include <string>

namespace Notes3D {

// Chaque fonction rend un JSON (UTF-8) : {"ok":true,...} ou {"ok":false,"erreur":"..."}.

// {"ok", "dossier", "projet", "notes":[...]} : le contenu de notes.json du projet ouvert.
std::string Lister ();

// Cree une note : camera du moment, capture de la fenetre 3D (la fenetre 3D passe devant si besoin).
// {"ok", "note":{...}}
std::string Noter (const std::string& texteUtf8);

// Remet la fenetre 3D sur la camera de la note. {"ok", "camera":{... relue apres changement}}
std::string AllerA (const std::string& id);

// Range la note et ses images dans notes-3d\supprimees (rien n'est efface). {"ok"}
std::string Supprimer (const std::string& id);

// Remplace les traits de la note (JSON : [{"couleur":"#rrggbb","epaisseur":px,"points":[{"x","y"},...]}]) et
// redessine <id>_annot.png depuis la capture brute. Liste vide : plus d'image annotee. {"ok", "note":{...}}
std::string EnregistrerTraits (const std::string& id, const std::string& traitsJson);

// La capture brute de la note en data URL (data:image/png;base64,...), "" si absente.
std::string ImageDataUrl (const std::string& id);

// Camera courante, au format des notes (pour les tests et l'API).
std::string CameraCourante ();

// Dossier des notes du projet ouvert (cree au besoin).
GS::UniString DossierNotes ();

// Outils
std::string VersUtf8 (const GS::UniString& s);
GS::UniString DepuisUtf8 (const std::string& s);
std::string Erreur (const std::string& message);

void Demarrer ();   // GDI+
void Arreter ();

}

#endif
