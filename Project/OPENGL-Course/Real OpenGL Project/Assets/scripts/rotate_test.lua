local luaTestBox = gameObject

function Update(dt)
		print(luaTestBox.transform.rotation.x)
		luaTestBox.transform.rotation.x = luaTestBox.transform.rotation.x + 1
end