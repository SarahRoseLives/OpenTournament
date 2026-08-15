// OTMapExporter - dumps a UT2004 map's BSP geometry to the game log so it can
// be converted to the OpenTournament .otmap format.
//
// Usage: run the game (or a dedicated server) with
//   ?Mutator=OTMapExporter.OTMapExporter
// and parse the "OTMAP|..." lines from System\UT2004.log.
class OTMapExporter extends Mutator;

function PostBeginPlay()
{
    Super.PostBeginPlay();
    Log("OTMAP|BEGIN");
    ExportMap();
    Log("OTMAP|END");
    Log("OTMAP|DONE");
}

function ExportMap()
{
    local int i;
    local string MatName;

    Log("OTMAP|meta " $ XLevel.Model.Points.Length $ " " $ XLevel.Model.Nodes.Length $ " " $ XLevel.Model.Verts.Length $ " " $ XLevel.Model.Surfs.Length);

    // Vertex pool (positions).
    for (i = 0; i < XLevel.Model.Points.Length; i++)
    {
        Log("OTMAP|point " $ i $ " " $ XLevel.Model.Points[i].X $ " " $ XLevel.Model.Points[i].Y $ " " $ XLevel.Model.Points[i].Z);
    }

    // BSP nodes (raw fields).
    for (i = 0; i < XLevel.Model.Nodes.Length; i++)
    {
        Log("OTMAP|node " $ i
            $ " " $ XLevel.Model.Nodes[i].Plane.X $ " " $ XLevel.Model.Nodes[i].Plane.Y $ " " $ XLevel.Model.Nodes[i].Plane.Z $ " " $ XLevel.Model.Nodes[i].Plane.W
            $ " " $ XLevel.Model.Nodes[i].iVertPool
            $ " " $ XLevel.Model.Nodes[i].iSurf
            $ " " $ XLevel.Model.Nodes[i].iVertex
            $ " " $ XLevel.Model.Nodes[i].iCollisionBound
            $ " " $ XLevel.Model.Nodes[i].iZone[0] $ " " $ XLevel.Model.Nodes[i].iZone[1]
            $ " " $ XLevel.Model.Nodes[i].iLeaf[0] $ " " $ XLevel.Model.Nodes[i].iLeaf[1]
            $ " " $ XLevel.Model.Nodes[i].NumVertices
            $ " " $ XLevel.Model.Nodes[i].NodeFlags);
    }

    // BSP verts (indices into the point pool).
    for (i = 0; i < XLevel.Model.Verts.Length; i++)
    {
        Log("OTMAP|vert " $ i $ " " $ XLevel.Model.Verts[i].pVertex $ " " $ XLevel.Model.Verts[i].iSide);
    }

    // BSP surfaces (material + flags).
    for (i = 0; i < XLevel.Model.Surfs.Length; i++)
    {
        if (XLevel.Model.Surfs[i].Material != None)
            MatName = string(XLevel.Model.Surfs[i].Material);
        else
            MatName = "None";
        Log("OTMAP|surf " $ i
            $ " " $ MatName
            $ " " $ XLevel.Model.Surfs[i].PolyFlags
            $ " " $ XLevel.Model.Surfs[i].pBase
            $ " " $ XLevel.Model.Surfs[i].vNormal $ " " $ XLevel.Model.Surfs[i].vTextureU $ " " $ XLevel.Model.Surfs[i].vTextureV
            $ " " $ XLevel.Model.Surfs[i].iBrushPoly
            $ " " $ XLevel.Model.Surfs[i].Actor);
    }
}

defaultproperties
{
    FriendlyName="OpenTournament Map Exporter"
    Description="Dumps the map BSP to the log for OpenTournament."
}
