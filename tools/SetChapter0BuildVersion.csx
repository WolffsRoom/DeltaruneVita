using System;
using System.Linq;
using UndertaleModLib.Models;
using UndertaleModLib.Compiler;

const string codeName = "gml_Object_obj_ui_version_Create_0";
string[] oldLines = {
    "_version_text = \"DELTARUNE \" + get_version();",
    "_version_text = \"DELTARUNE v0.68 (Internal Build)\";",
    "_version_text = \"DELTARUNE VITA v0.69\";",
    "_version_text = \"DELTARUNE VITA v0.70\";",
    "_version_text = \"DELTARUNE VITA v0.71\";",
    "_version_text = \"DELTARUNE VITA v0.72\";",
    "_version_text = \"DELTARUNE VITA v0.73\";",
    "_version_text = \"DELTARUNE VITA v0.74\";",
    "_version_text = \"DELTARUNE VITA v0.75\";",
    "_version_text = \"DELTARUNE VITA v0.76\";",
    "_version_text = \"DELTARUNE VITA v0.77\";",
    "_version_text = \"DELTARUNE VITA v0.78\";"
};
const string newLine = "_version_text = \"DELTARUNE VITA v0.78\";";

UndertaleCode code = Data.Code.ByName(codeName);
if (code is null)
    throw new Exception($"Chapter 0 code not found: {codeName}");

string source = GetDecompiledText(codeName);
string oldLine = oldLines.FirstOrDefault(line => source.Contains(line));
if (oldLine is null)
    throw new Exception("Chapter 0 version assignment was not recognized.");

if (!source.Contains(newLine))
{
    CodeImportGroup importGroup = new(Data, null, Data.ToolInfo.DecompilerSettings)
    {
        ThrowOnNoOpFindReplace = true,
        MainThreadAction = MainThreadAction
    };
    importGroup.QueueFindReplace(code, oldLine, newLine, true);
    importGroup.Import();
}

Console.WriteLine("Chapter 0 build label: DELTARUNE VITA v0.78");
