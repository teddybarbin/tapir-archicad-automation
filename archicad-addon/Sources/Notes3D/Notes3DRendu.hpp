// Notes 3D Imaginns : dessin des traits sur la capture (GDI+). Ce fichier n'inclut aucun en-tete d'ArchiCAD.
#ifndef NOTES3D_RENDU_HPP
#define NOTES3D_RENDU_HPP

#include <string>
#include <vector>

namespace Rendu {

struct Point { double x; double y; };

struct Trait {
	unsigned char r = 255, g = 23, b = 68;
	double epaisseur = 6.0;          // en pixels de l'image
	std::vector<Point> points;       // en pixels de l'image
};

void Demarrer ();
void Arreter ();

// Taille d'une image ; false si illisible.
bool Taille (const wchar_t* chemin, int& largeur, int& hauteur);

// Ecrit dans `destination` la capture `source` avec les traits par-dessus (PNG). false et `erreur` si echec.
bool DessinerTraits (const wchar_t* source, const wchar_t* destination, const std::vector<Trait>& traits, std::string& erreur);

}

#endif
