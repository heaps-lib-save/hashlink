package sdl;

private typedef SurfacePtr = hl.Abstract<"sdl_surface">;

@:hlNative("sdl")
abstract Surface(SurfacePtr) {

	public var width(get,never) : Int;
	public var height(get,never) : Int;
	public var pixels(get,never) : hl.Bytes;

	public inline function free() {
		freeSurface(this);
		this = null;
	}

	public inline function get_width() : Int {
		return surfaceGetWidth(this);
	}

	public inline function get_height() : Int {
		return surfaceGetHeight(this);
	}

	public inline function get_pixels() : hl.Bytes {
		return surfaceGetPixels(this);
	}

	public static function fromBGRA( pixels : hl.Bytes, width : Int, height : Int ) : Surface {
		return createRGBSurface(width, height, 32, 0xFF0000, 0xFF00, 0xFF, 0xFF000000);
	}

	// Native bindings
	static function freeSurface(p:SurfacePtr) {}
	static function createRGBSurface( width : Int, height : Int, depth : Int, rmask : Int, gmask : Int, bmask : Int, amask : Int ) : SurfacePtr { return null; }
	static function surfaceGetWidth( p : SurfacePtr ) : Int { return 0; }
	static function surfaceGetHeight( p : SurfacePtr ) : Int { return 0; }
	static function surfaceGetPixels( p : SurfacePtr ) : hl.Bytes { return null; }
}
