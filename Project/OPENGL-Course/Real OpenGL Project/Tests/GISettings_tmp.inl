
		


		for (int i = 0; i < 3; i++) {
			std::string n = std::to_string(i);
			if (j.contains("lod" + n + "Enabled")) gs.lodEnabled[i] = j["lod" + n + "Enabled"];
			if (j.contains("lod" + n + "Distance")) gs.lodDistance[i] = j["lod" + n + "Distance"];
		}
		if (j.contains("lodCrossfade")) gs.lodCrossfade = j["lodCrossfade"];
		if (j.contains("taaEnabled")) gs.taaEnabled = j["taaEnabled"];
		if (j.contains("taaSharpness")) gs.taaSharpness = j["taaSharpness"];
		if (j.contains("aoEnabled")) gs.aoEnabled = j["aoEnabled"];
		if (j.contains("ssrEnabled")) gs.ssrEnabled = j["ssrEnabled"];
		if (j.contains("giEnabled")) gs.giEnabled = j["giEnabled"];
		if (j.contains("giQuality")) gs.giQuality = j["giQuality"];
		if (j.contains("giIntensity")) gs.giIntensity = j["giIntensity"];
		if (j.contains("giBounce")) gs.giBounce = j["giBounce"];
		if (j.contains("giSkyColour")) gs.giSkyColour = j["giSkyColour"];
		if (j.contains("ssrMaxDistance")) gs.ssrMaxDistance = j["ssrMaxDistance"];
		if (j.contains("probeFacesPerFrame")) gs.probeFacesPerFrame = j["probeFacesPerFrame"];
		if (j.contains("aoRadius")) gs.aoRadius = j["aoRadius"];
		if (j.contains("aoStrength")) gs.aoStrength = j["aoStrength"];
		if (j.contains("tonemapper")) gs.tonemapper = j["tonemapper"];
		if (j.contains("autoExposure")) gs.autoExposure = j["autoExposure"];
		if (j.contains("exposureCompensation")) gs.exposureCompensation = j["exposureCompensation"];
		if (j.contains("exposureKey")) gs.exposureKey = j["exposureKey"];
		if (j.contains("exposureStrength")) gs.exposureStrength = j["exposureStrength"];
		if (j.contains("exposureRange")) gs.exposureRange = j["exposureRange"];
		if (j.contains("exposureAdaptSpeed")) gs.exposureAdaptSpeed = j["exposureAdaptSpeed"];
		if (j.contains("renderDistance")) gs.renderDistance = j["renderDistance"];
		if (j.contains("detailDistance")) gs.detailDistance = j["detailDistance"];
		if (j.contains("detailDensity")) gs.detailDensity = j["detailDensity"];
		if (j.contains("impostors")) gs.impostors = j["impostors"];
		if (j.contains("impostorResolution")) gs.impostorResolution = j["impostorResolution"];
		if (j.contains("waterDetail")) gs.waterDetail = j["waterDetail"];
		if (j.contains("windSpeed")) gs.windSpeed = j["windSpeed"];
		if (j.contains("windDirection")) gs.windDirection = j["windDirection"];
		if (j.contains("windGustiness")) gs.windGustiness = j["windGustiness"];
		if (j.contains("windTurbulence")) gs.windTurbulence = j["windTurbulence"];
		if (j.contains("windTreeSway")) gs.windTreeSway = j["windTreeSway"];
		if (j.contains("windLeafFlutter")) gs.windLeafFlutter = j["windLeafFlutter"];
		if (j.contains("windGrassBend")) gs.windGrassBend = j["windGrassBend"];
		if (j.contains("shadowDistance")) gs.shadowDistance = j["shadowDistance"];
		if (j.contains("shadowCascades")) gs.shadowCascades = j["shadowCascades"];
		if (j.contains("shadowResolution")) gs.shadowResolution = j["shadowResolution"];
		if (j.contains("shadowCascadeSplit")) gs.shadowCascadeSplit = j["shadowCascadeSplit"];
		if (j.contains("shadowLodBias")) gs.shadowLodBias = j["shadowLodBias"];
		if (j.contains("detailShadowDistance")) gs.detailShadowDistance = j["detailShadowDistance"];

		if (j.contains("godraysEnabled")) gs.godraysEnabled = j["godraysEnabled"];
		if (j.contains("godraysDecay")) gs.godraysDecay = j["godraysDecay"];
		if (j.contains("godraysDensity")) gs.godraysDensity = j["godraysDensity"];

		if (j.contains("volumetricFogEnabled")) gs.volumetricFogEnabled = j["volumetricFogEnabled"];
		if (j.contains("fogQuality")) gs.fogQuality = j["fogQuality"];
		if (j.contains("fogDensity")) gs.fogDensity = j["fogDensity"];
		if (j.contains("fogBaseHeight")) gs.fogBaseHeight = j["fogBaseHeight"];
		if (j.contains("fogHeightFalloff")) gs.fogHeightFalloff = j["fogHeightFalloff"];
		if (j.contains("fogRange")) gs.fogRange = j["fogRange"];
		if (j.contains("fogAnisotropy")) gs.fogAnisotropy = j["fogAnisotropy"];
		if (j.contains("fogAlbedo")) gs.fogAlbedo = glm::vec3(j["fogAlbedo"][0].get<float>(), j["fogAlbedo"][1].get<float>(), j["fogAlbedo"][2].get<float>());
		if (j.contains("fogSunIntensity")) gs.fogSunIntensity = j["fogSunIntensity"];
		if (j.contains("physicalAtmosphere")) gs.physicalAtmosphere = j["physicalAtmosphere"];
		if (j.contains("atmosphereVisibility")) gs.atmosphereVisibility = j["atmosphereVisibility"];
		if (j.contains("aerialPerspectiveScale")) gs.aerialPerspectiveScale = j["aerialPerspectiveScale"];
		if (j.contains("skyIntensity")) gs.skyIntensity = j["skyIntensity"];
		if (j.contains("timeOfDayExposure")) gs.timeOfDayExposure = j["timeOfDayExposure"];
		if (j.contains("fogAmbientIntensity")) gs.fogAmbientIntensity = j["fogAmbientIntensity"];
		if (j.contains("fogNoiseStrength")) gs.fogNoiseStrength = j["fogNoiseStrength"];
		if (j.contains("fogNoiseScale")) gs.fogNoiseScale = j["fogNoiseScale"];
		if (j.contains("fogWindSpeed")) gs.fogWindSpeed = j["fogWindSpeed"];
		if (j.contains("fogTemporalBlend")) gs.fogTemporalBlend = std::max(0.05f, j["fogTemporalBlend"].get<float>());

		if (j.contains("volumetricSkyEnabled")) gs.volumetricSkyEnabled = j["volumetricSkyEnabled"];
		if (j.contains("cloudsEnabled")) gs.cloudsEnabled = j["cloudsEnabled"];
		if (j.contains("cloudsDensity")) gs.cloudsDensity = j["cloudsDensity"];
		if (j.contains("cloudsLayer")) gs.cloudsLayer = j["cloudsLayer"];
		if (j.contains("cloudsSpeed")) gs.cloudsSpeed = j["cloudsSpeed"];
		if (j.contains("cloudsSharpness")) gs.cloudsSharpness = j["cloudsSharpness"];
		if (j.contains("cloudShadowsEnabled")) gs.cloudShadowsEnabled = j["cloudShadowsEnabled"];
		if (j.contains("cloudShadowStrength")) gs.cloudShadowStrength = j["cloudShadowStrength"];

		if (j.contains("skyboxType")) gs.skyboxType = (SkyboxType)j["skyboxType"];

		if (j.contains("universeStarDensity")) gs.universeStarDensity = j["universeStarDensity"];
		if (j.contains("universeStarBrightness")) gs.universeStarBrightness = j["universeStarBrightness"];
		if (j.contains("universeNebulaIntensity")) gs.universeNebulaIntensity = j["universeNebulaIntensity"];
		if (j.contains("universeSpeed")) gs.universeSpeed = j["universeSpeed"];
		
		if (j.contains("universeNebulaColor1")) {
			auto& c = j["universeNebulaColor1"];
			gs.universeNebulaColor1 = glm::vec3(c[0], c[1], c[2]);
		}
		if (j.contains("universeNebulaColor2")) {
			auto& c = j["universeNebulaColor2"];
			gs.universeNebulaColor2 = glm::vec3(c[0], c[1], c[2]);
		}

		if (j.contains("debugLODColoring")) gs.debugLODColoring = j["debugLODColoring"];
		if (j.contains("debugShowBounds")) gs.debugShowBounds = j["debugShowBounds"];
		if (j.contains("debugFreezeCulling")) gs.debugFreezeCulling = j["debugFreezeCulling"];
		if (j.contains("showWireframe")) gs.showWireframe = j["showWireframe"];
		if (j.contains("enableOcclusionCulling")) gs.enableOcclusionCulling = j["enableOcclusionCulling"];
		if (j.contains("debugShowHiZ")) gs.debugShowHiZ = j["debugShowHiZ"];
		if (j.contains("debugShowCulling")) gs.debugShowCulling = j["debugShowCulling"];
	
