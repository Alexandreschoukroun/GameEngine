#version 460 core

// Volontairement vide. Le framebuffer d'ombre n'a aucune sortie couleur : le GPU ne
// remplit que le tampon de profondeur, qu'il ecrit tout seul. C'est ce qui rend cette
// passe tres rapide.
void main() {}
