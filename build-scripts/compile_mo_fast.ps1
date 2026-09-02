param(
    [Parameter(Mandatory = $true)][string]$InputPo,
    [Parameter(Mandatory = $true)][string]$OutputMo,
    [switch]$Force
)
$ErrorActionPreference = 'Stop'
$inputPath = [IO.Path]::GetFullPath($InputPo)
$outputPath = [IO.Path]::GetFullPath($OutputMo)
if (!(Test-Path -LiteralPath $inputPath)) { throw "PO file not found: $inputPath" }
if (!$Force -and (Test-Path -LiteralPath $outputPath) -and
    (Get-Item -LiteralPath $outputPath).LastWriteTimeUtc -ge
    (Get-Item -LiteralPath $inputPath).LastWriteTimeUtc) { Write-Host "MO is current: $outputPath"; exit 0 }

Add-Type -Language CSharp -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
public static class CddaPoCompiler {
 sealed class E { public string C,I,P,F=""; public bool O; public readonly SortedDictionary<int,string> V=new SortedDictionary<int,string>(); public void Clear(){C=I=P=null;F="";O=false;V.Clear();} }
 static string D(string s){int a=s.IndexOf('"'),b=s.LastIndexOf('"');if(a<0||b<=a)return "";var r=new StringBuilder();for(int i=a+1;i<b;i++){char c=s[i];if(c!='\\'||i+1>=b){r.Append(c);continue;}char n=s[++i];r.Append(n=='n'?'\n':n=='r'?'\r':n=='t'?'\t':n);}return r.ToString();}
 static void A(E e,string p){if(e.F=="C")e.C=(e.C??"")+p;else if(e.F=="I")e.I=(e.I??"")+p;else if(e.F=="P")e.P=(e.P??"")+p;else if(e.F.StartsWith("V")){int n=int.Parse(e.F.Substring(1));e.V[n]=(e.V.ContainsKey(n)?e.V[n]:"")+p;}}
 static void Z(E e,SortedDictionary<string,string> c){if(e.O||e.I==null||e.V.Count==0)return;string k=e.I;if(e.P!=null)k+="\0"+e.P;if(e.C!=null)k=e.C+"\x04"+k;int m=e.V.Keys.Max();string v=String.Join("\0",Enumerable.Range(0,m+1).Select(i=>e.V.ContainsKey(i)?e.V[i]:""));if(e.I.Length==0||v.Length!=0)c[k]=v;}
 static void U(BinaryWriter w,long n){w.Write((UInt32)n);}
 public static int Compile(string input,string output){var c=new SortedDictionary<string,string>(StringComparer.Ordinal);var e=new E();foreach(string l in File.ReadLines(input,Encoding.UTF8)){if(l.Length==0){Z(e,c);e.Clear();continue;}if(l.StartsWith("#~")){e.O=true;continue;}if(l.StartsWith("#"))continue;if(l.StartsWith("msgctxt ")){e.C=D(l);e.F="C";continue;}if(l.StartsWith("msgid_plural ")){e.P=D(l);e.F="P";continue;}if(l.StartsWith("msgid ")){e.I=D(l);e.F="I";continue;}if(l.StartsWith("msgstr")){int n=0;if(l.StartsWith("msgstr[")){int a=l.IndexOf('['),b=l.IndexOf(']');n=int.Parse(l.Substring(a+1,b-a-1));}e.V[n]=D(l);e.F="V"+n;continue;}if(l.StartsWith("\""))A(e,D(l));}Z(e,c);var u=new UTF8Encoding(false);var o=c.Select(x=>u.GetBytes(x.Key)).ToList();var t=c.Select(x=>u.GetBytes(x.Value)).ToList();long ot=28,tt=ot+8L*c.Count,ss=tt+8L*c.Count,ts=ss+o.Sum(x=>(long)x.Length+1);Directory.CreateDirectory(Path.GetDirectoryName(output));string tmp=output+".tmp";using(var s=new FileStream(tmp,FileMode.Create,FileAccess.Write))using(var w=new BinaryWriter(s)){U(w,0x950412de);U(w,0);U(w,c.Count);U(w,ot);U(w,tt);U(w,0);U(w,0);long p=ss;foreach(var b in o){U(w,b.Length);U(w,p);p+=b.Length+1;}p=ts;foreach(var b in t){U(w,b.Length);U(w,p);p+=b.Length+1;}foreach(var b in o){w.Write(b);w.Write((byte)0);}foreach(var b in t){w.Write(b);w.Write((byte)0);}}File.Copy(tmp,output,true);File.Delete(tmp);return c.Count;}
}
'@
$count = try { [CddaPoCompiler]::Compile($inputPath, $outputPath) } catch {
    Write-Error ($_.Exception.InnerException.ToString())
    throw
}
Write-Host "Compiled $count messages: $outputPath"
