xof 0303txt 0032

Mesh MissingTextureMesh {
 3;
 0.0;0.0;0.0;,
 1.0;0.0;0.0;,
 0.0;1.0;0.0;;
 1;
 3;0,1,2;;
 MeshMaterialList {
  2;
  1;
  0;;
  Material {
   1.0;1.0;1.0;1.0;;
   0.0;
   0.0;0.0;0.0;;
   0.0;0.0;0.0;;
   TextureFilename { "missing-texture.png"; }
  }
  Material {
   1.0;1.0;1.0;1.0;;
   0.0;
   0.0;0.0;0.0;;
   0.0;0.0;0.0;;
   TextureFilename { "exists-but-invalid-texture.png"; }
  }
 }
}
