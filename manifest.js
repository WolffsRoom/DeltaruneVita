// Seam's Patcher manifest loader.
// The large manifest is kept in manifest.json so the web patcher can load it
// asynchronously and keep small runtime hotfixes separate from generated data.
var manifestData = null;
var manifestReady = fetch("manifest.json?v=0.73.1", {cache:"no-store"})
  .then(function(res){
    if(!res.ok) throw new Error("Unable to load manifest.json (HTTP "+res.status+")");
    return res.json();
  })
  .then(function(data){
    manifestData = data;
    return data;
  })
  .catch(function(err){
    console.error("Failed to load DeltaruneVita manifest", err);
    throw err;
  });

// The splash appears before the website-language selector, so keep this line
// language-neutral/English instead of inheriting an old PT-BR hardcoded label.
var seamSplashPress = document.querySelector("#splash .press");
if(seamSplashPress) seamSplashPress.textContent = "Click or press a key to continue";

// Load web-only fixes after the inline patcher code has had a chance to define
// its functions. The hotfix retries installation until those functions exist.
(function(){
  var script=document.createElement("script");
  script.src="seams-hotfix.js?v=0.73.1";
  script.async=true;
  script.onerror=function(){ console.error("Failed to load Seam's Patcher hotfix"); };
  document.head.appendChild(script);
})();
