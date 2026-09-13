(function(){
  "use strict";

  const HOTFIX_VERSION = "0.73.1";

  function install(){
    if(typeof window.generate !== "function" || typeof window.applySelectedLanguagePacks !== "function" || typeof window.JSZip === "undefined"){
      setTimeout(install, 25);
      return;
    }

    const splashPress=document.querySelector("#splash .press");
    if(splashPress) splashPress.textContent="Click or press a key to continue";

    const pbar=document.getElementById("pbar");
    if(pbar){
      pbar.style.width="62%";
      pbar.style.height="6%";
      const ptext=pbar.querySelector(".ptext");
      if(ptext){
        ptext.style.fontSize="0.68em";
        ptext.style.padding="0 .5em";
        ptext.style.whiteSpace="nowrap";
      }
    }

    function progressText(percent,label){
      const bar=document.getElementById("pbar");
      if(!bar) return;
      const rounded=Math.round(Math.max(0,Math.min(100,percent||0)));
      const fill=bar.firstElementChild;
      if(fill) fill.style.width=rounded+"%";
      const text=bar.querySelector(".ptext");
      if(text) text.textContent=label ? label+"  •  "+rounded+"%" : rounded+"%";
    }

    function formatMB(bytes){
      return (bytes/(1024*1024)).toFixed(bytes >= 100*1024*1024 ? 0 : 1)+" MB";
    }

    async function fetchArrayBufferWithProgress(url,options,onProgress){
      const res=await fetch(url,options||{});
      if(!res.ok) return {res, buffer:null};
      const expected=Number(res.headers.get("content-length"))||0;
      if(!res.body || typeof res.body.getReader !== "function"){
        const buffer=await res.arrayBuffer();
        if(onProgress) onProgress(buffer.byteLength, expected||buffer.byteLength);
        return {res,buffer};
      }
      const reader=res.body.getReader();
      const chunks=[];
      let received=0;
      while(true){
        const {done,value}=await reader.read();
        if(done) break;
        if(!value) continue;
        chunks.push(value);
        received+=value.byteLength;
        if(onProgress) onProgress(received,expected);
      }
      const merged=new Uint8Array(received);
      let offset=0;
      for(const chunk of chunks){ merged.set(chunk,offset); offset+=chunk.byteLength; }
      return {res,buffer:merged.buffer};
    }

    async function loadManifest(){
      if(window.manifestReady){
        try{ await window.manifestReady; }catch(e){}
      }
      if(typeof manifestData !== "undefined" && manifestData) return manifestData;
      const res=await fetch("manifest.json?v="+encodeURIComponent(HOTFIX_VERSION),{cache:"no-store"});
      if(!res.ok) throw new Error("Unable to load manifest.json (HTTP "+res.status+")");
      const data=await res.json();
      window.manifestData=data;
      return data;
    }

    window.applySelectedLanguagePacks = async function(zip){
      if(!selectedExtras.size) return;
      const catalog=await loadLanguageCatalog();
      const selectedCodes=Array.from(selectedExtras);
      const totalLanguages=selectedCodes.length;

      for(let langIndex=0; langIndex<selectedCodes.length; langIndex++){
        const code=selectedCodes[langIndex];
        const L=LANGS.find(x=>x.code===code);
        if(!L || !L.packId) continue;
        const entry=(catalog.languages||[]).find(x=>x.id===L.packId && x.ready);
        if(!entry || (!entry.url && !(Array.isArray(entry.parts) && entry.parts.length))) throw new Error("Language Pack unavailable: "+L.label);

        const langPrefix="Language "+(langIndex+1)+"/"+totalLanguages+" - "+L.label;
        setLine(document.getElementById("single"), "* Downloading "+langPrefix+"...");
        progressText(0,langPrefix);

        let packSource=null;
        let pack=null;

        if(Array.isArray(entry.parts) && entry.parts.length){
          const parts=[];
          const totalExpected=entry.parts.reduce((sum,p)=>sum+(Number(p.size)||0),0) || Number(entry.size)||0;
          let completedBytes=0;

          for(let pi=0; pi<entry.parts.length; pi++){
            const part=entry.parts[pi];
            const partUrl=new URL(part.url,location.href).href;
            const partBase=completedBytes;
            const partExpected=Number(part.size)||0;
            const result=await fetchArrayBufferWithProgress(partUrl,{cache:"no-store"},(received,headerExpected)=>{
              const expectedTotal=totalExpected || (partBase + (headerExpected||partExpected||received));
              const current=partBase+received;
              const pct=expectedTotal ? (current/expectedTotal)*100 : ((pi+received/Math.max(1,headerExpected||partExpected||received))/entry.parts.length)*100;
              const partLabel=langPrefix+" - part "+(pi+1)+"/"+entry.parts.length;
              progressText(pct,partLabel);
              setLine(document.getElementById("single"), "* Downloading "+langPrefix+"\n  Part "+(pi+1)+"/"+entry.parts.length+" - "+formatMB(current)+(expectedTotal?" / "+formatMB(expectedTotal):""));
            });
            if(!result.res.ok) throw new Error("Failed to download "+L.label+" part "+(pi+1)+" (HTTP "+result.res.status+")");
            const partBuf=result.buffer;
            if(part.size && partBuf.byteLength!==part.size) throw new Error("Invalid size: "+L.label+" part "+(pi+1));
            if(part.sha256 && (await sha256Hex(partBuf)).toLowerCase()!==part.sha256.toLowerCase()) throw new Error("Invalid SHA-256: "+L.label+" part "+(pi+1));
            parts.push(partBuf);
            completedBytes+=partBuf.byteLength;
          }

          packSource=new Blob(parts,{type:"application/zip"});
          parts.length=0;
          if(entry.size && packSource.size!==entry.size) throw new Error("Invalid language pack size: "+L.label);
          // Every multipart chunk is individually SHA-256 validated above. Avoid
          // rebuilding the whole pack into another giant contiguous ArrayBuffer.
          pack=await JSZip.loadAsync(packSource);
        }else{
          const packFetchOptions={cache:"no-store"};
          if(entry.api_asset || entry.url.includes("api.github.com/repos/")){
            packFetchOptions.headers={"Accept":"application/octet-stream","X-GitHub-Api-Version":"2022-11-28"};
          }
          let result=await fetchArrayBufferWithProgress(entry.url,packFetchOptions,(received,expected)=>{
            const total=expected||Number(entry.size)||received;
            progressText(total?(received/total)*100:0,langPrefix);
            setLine(document.getElementById("single"), "* Downloading "+langPrefix+"\n  "+formatMB(received)+(total?" / "+formatMB(total):""));
          });
          if(!result.res.ok && location.hostname==="127.0.0.1"){
            result=await fetchArrayBufferWithProgress("/proxy_download?url="+encodeURIComponent(entry.url),{cache:"no-store"},(received,expected)=>{
              const total=expected||Number(entry.size)||received;
              progressText(total?(received/total)*100:0,langPrefix);
            });
          }
          if(!result.res.ok) throw new Error("Failed to download "+L.label+" (HTTP "+result.res.status+")");
          packSource=result.buffer;
          if(entry.size && packSource.byteLength!==entry.size) throw new Error("Invalid language pack size: "+L.label);
          if(entry.sha256 && (await sha256Hex(packSource)).toLowerCase()!==entry.sha256.toLowerCase()) throw new Error("Invalid SHA-256: "+L.label);
          pack=await JSZip.loadAsync(packSource);
        }

        const manifestEntry=pack.file("manifest.json");
        if(!manifestEntry) throw new Error("manifest.json missing from "+L.label+" language pack");
        const mf=JSON.parse(await manifestEntry.async("string"));
        const root=manifest.output_folder+"/deltarunevita/mods/Lang/"+L.packId+"/";
        const records=mf.records||[];

        setLine(document.getElementById("single"), "* Preparing "+langPrefix+"...");
        for(let i=0;i<records.length;i++){
          const rec=records[i];
          const pct=(i/Math.max(1,records.length))*100;
          progressText(pct,langPrefix+" - preparing");
          let data;
          if(rec.mode==="extra"){
            const extraEntry=pack.file(rec.extra);
            if(!extraEntry) throw new Error("Language pack file missing: "+rec.extra);
            data=new Uint8Array(await extraEntry.async("arraybuffer"));
          }else{
            const steamEntry=findSteamEntry(rec.source);
            if(!steamEntry) throw new Error("Steam file missing: "+rec.source);
            const oldBuf=new Uint8Array(await steamEntryArrayBuffer(steamEntry));
            if(rec.source_size && oldBuf.length!==rec.source_size) throw new Error("Incompatible Steam file: "+rec.source);
            if(rec.source_sha256 && (await sha256Hex(oldBuf)).toLowerCase()!==rec.source_sha256.toLowerCase()) throw new Error("Incompatible Steam file: "+rec.source);
            if(rec.mode==="copy"){
              data=oldBuf;
            }else{
              const patchEntry=pack.file(rec.browser_patch);
              if(!patchEntry) throw new Error("Language patch missing: "+rec.browser_patch);
              data=await applyBrowserPatch(oldBuf,new Uint8Array(await patchEntry.async("arraybuffer")));
            }
          }
          if(rec.output_sha256 && (await sha256Hex(data)).toLowerCase()!==rec.output_sha256.toLowerCase()) throw new Error("Validation failed: "+L.label+"/"+rec.output);
          zip.file(root+rec.output,data);

          if((i&3)===3) await new Promise(r=>setTimeout(r,0));
        }

        progressText(100,langPrefix+" - ready");
        setLine(document.getElementById("single"), "* "+langPrefix+" ready.");
        pack=null;
        packSource=null;
        await new Promise(r=>setTimeout(r,0));
      }
    };

    window.generate = async function(mirrorId,attachedZipFile){
      busy=true;
      setBar(0);
      showBar(true);
      try{
        manifest=await loadManifest();
        setLine(document.getElementById("single"), "* "+((UI_TEXT[webLang]||UI_TEXT.en).progress_processing||"Processing DELTARUNE files..."));
        const zip=new JSZip();
        const totalFiles=manifest.files.length;

        for(let i=0;i<totalFiles;i++){
          const fileRec=manifest.files[i];
          progressText((i/Math.max(1,totalFiles))*100,"Base files");
          const fileOutput=fileRec.output||fileRec.path;
          if(!fileOutput) throw new Error("Invalid manifest: base file without path at position "+i);
          const outPath=manifest.output_folder+"/"+fileOutput;

          if(fileRec.mode==="embed"){
            const res=await fetch("embedded/"+fileOutput,{cache:"no-store"});
            if(!res.ok) throw new Error("Base file missing: "+fileOutput+" (HTTP "+res.status+")");
            const buf=await res.arrayBuffer();
            if(fileRec.size && buf.byteLength!==fileRec.size) throw new Error("Invalid base file size: "+fileOutput);
            if(fileRec.sha256 && (await sha256Hex(buf)).toLowerCase()!==fileRec.sha256.toLowerCase()) throw new Error("Invalid base file SHA-256: "+fileOutput);
            zip.file(outPath,buf);
          }else if(fileRec.mode==="copy" || fileRec.mode==="browser_patch"){
            const steamEntry=findSteamEntry(fileRec.source);
            if(!steamEntry) throw new Error("Steam file missing: "+fileRec.source);
            const srcBuf=new Uint8Array(await steamEntryArrayBuffer(steamEntry));
            const req=(manifest.required_sources||{})[fileRec.source];
            if(req && req.size && srcBuf.length!==req.size) throw new Error("Incompatible Steam file: "+fileRec.source);
            if(req && req.sha256 && (await sha256Hex(srcBuf)).toLowerCase()!==req.sha256.toLowerCase()) throw new Error("Incompatible Steam file: "+fileRec.source);
            if(fileRec.mode==="copy"){
              zip.file(outPath,srcBuf);
            }else{
              const patchRes=await fetch("patch_data/patches/"+fileRec.patch,{cache:"no-store"});
              if(!patchRes.ok) throw new Error("Patch missing: "+fileRec.patch);
              const patchBuf=new Uint8Array(await patchRes.arrayBuffer());
              const patchedBuf=await applyBrowserPatch(srcBuf,patchBuf);
              if(fileRec.sha256 && (await sha256Hex(patchedBuf)).toLowerCase()!==fileRec.sha256.toLowerCase()) throw new Error("Generated file validation failed: "+fileOutput);
              zip.file(outPath,patchedBuf);
            }
          }else{
            throw new Error("Unknown manifest mode: "+fileRec.mode);
          }
        }

        await window.applySelectedLanguagePacks(zip);

        const T=UI_TEXT[webLang]||UI_TEXT.en;
        setLine(document.getElementById("single"), "* "+(T.progress_compressing||"Compressing final files...")+"\n  Large multi-language packages may take a while.");
        progressText(0,"Final ZIP");

        // Always use JSZip's internal streaming output. generateAsync(type:blob)
        // may request one huge contiguous ArrayBuffer for multi-language builds,
        // which is what caused "Array buffer allocation failed" in Chromium/Edge.
        const chunks=[];
        const content=await new Promise((resolve,reject)=>{
          const stream=zip.generateInternalStream({type:"uint8array",streamFiles:true});
          stream.on("data",(chunk,metadata)=>{
            chunks.push(chunk);
            if(metadata && Number.isFinite(metadata.percent)) progressText(metadata.percent,"Final ZIP");
          });
          stream.on("error",reject);
          stream.on("end",()=>{
            const blob=new Blob(chunks,{type:"application/zip"});
            chunks.length=0;
            resolve(blob);
          });
          stream.resume();
        });

        const link=document.createElement("a");
        const downloadUrl=URL.createObjectURL(content);
        link.href=downloadUrl;
        link.download="DeltaruneVita_v"+PORT_VERSION+".zip";
        link.style.display="none";
        document.body.appendChild(link);
        link.click();
        setTimeout(()=>{ URL.revokeObjectURL(downloadUrl); link.remove(); },60000);

        showBar(false);
        setSeam("laugh",2000);
        setLine(document.getElementById("single"),SEAM_LINES.done);
      }catch(e){
        console.error("Error generating patch",e);
        showBar(false);
        setSeam("oh",2000);
        const T=UI_TEXT[webLang]||UI_TEXT.en;
        let message=T.processing_error||"An error occurred during processing. Check the console.";
        if(e && /Array buffer allocation failed|out of memory|allocation/i.test(String(e.message||e))){
          message="The browser ran out of memory while building the package. Close other heavy tabs and try again. The v0.73.1 memory-safe path is active, so please report which languages were selected if this persists.";
        }
        setLine(document.getElementById("single"),"* "+message);
      }finally{
        busy=false;
      }
    };

    window.__seamsPatcherMemoryHotfix=HOTFIX_VERSION;
    console.info("Seam's Patcher memory/progress hotfix",HOTFIX_VERSION,"active");
  }

  install();
})();
