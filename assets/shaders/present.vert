#version 460 core

// Triangle plein ecran, sans aucune donnee de sommet : les trois positions sont deduites
// du numero du sommet. gl_VertexID vaut 0, 1, 2.
//
//   ID 0 -> uv (0,0) -> position (-1,-1)   coin bas gauche de l'ecran
//   ID 1 -> uv (2,0) -> position ( 3,-1)   tres loin a droite
//   ID 2 -> uv (0,2) -> position (-1, 3)   tres loin en haut
//
// Ce grand triangle deborde de l'ecran, qui se retrouve entierement couvert. Un rectangle
// en deux triangles ferait le meme travail, mais les pixels de la diagonale seraient
// traites deux fois, et il faudrait un buffer de sommets.

out vec2 vTexCoord;

void main() {
    vec2 uv = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vTexCoord = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
