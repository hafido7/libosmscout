//
//  OSMScoutMKTileOverlay.m
//  MyFreemap
//
//  Created by Vladimir Vyskocil on 06/11/2013.
//  Copyright (c) 2013 Bleuazur. All rights reserved.
//

#import "OSMScoutMKTileOverlay.h"

#define RENDER_QUEUE_MAX_LEN 80

@implementation OSMScoutMKTileOperation

-(id)initWithOsmScout: (OSMScout *)osmScout center:(CLLocationCoordinate2D) center zoom:(NSInteger)zoom scaleFactor: (CGFloat)scaleFactor result: (OSMScoutMKTileOperationCB)result {
    if((self = [super init])){
        executing = NO;
        finished = NO;
        _osmScout = osmScout;
#if !__has_feature(objc_arc)
        [_osmScout retain];
#endif
        _center = center;
        _zoom = zoom;
        _scaleFactor = scaleFactor;
        _result = [result copy];
    }
    return self;
}

#if !__has_feature(objc_arc)
-(void)dealloc{
    [_osmScout release];
    [super dealloc];
}
#endif

- (BOOL)isConcurrent {
    return YES;
}

- (BOOL)isExecuting {
    return executing;
}

- (BOOL)isFinished {
    return finished;
}

- (void)start {
    // Always check for cancellation before launching the task.
    if ([self isCancelled])
    {
        // Must move the operation to the finished state if it is canceled.
        [self willChangeValueForKey:@"isFinished"];
        finished = YES;
        [self didChangeValueForKey:@"isFinished"];
        return;
    }
    
    // If the operation is not canceled, begin executing the task.
    [self willChangeValueForKey:@"isExecuting"];
    [NSThread detachNewThreadSelector:@selector(main) toTarget:self withObject:nil];
    executing = YES;
    [self didChangeValueForKey:@"isExecuting"];
}

- (void)completeOperation {
    [self willChangeValueForKey:@"isFinished"];
    [self willChangeValueForKey:@"isExecuting"];
    
    executing = NO;
    finished = YES;
    
    [self didChangeValueForKey:@"isExecuting"];
    [self didChangeValueForKey:@"isFinished"];
}

-(void)main{
    
    @try {
        #if !__has_feature(objc_arc)
        NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
        #endif
#if TARGET_OS_IPHONE
        CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
        CGContextRef cg = CGBitmapContextCreate(NULL, kOSMScoutDefaultTileSize*_scaleFactor, kOSMScoutDefaultTileSize*_scaleFactor, 8, 0, space, kCGImageAlphaPremultipliedLast);
        if(cg){

            CGAffineTransform flipVertical = CGAffineTransformMake(1, 0, 0, -1, 0, kOSMScoutDefaultTileSize*_scaleFactor);
            CGContextConcatCTM(cg, flipVertical);
            UIGraphicsPushContext(cg);
            [_osmScout drawMapTo:cg lat:_center.latitude lon:_center.longitude zoom:1<<_zoom width:kOSMScoutDefaultTileSize*_scaleFactor height:kOSMScoutDefaultTileSize*_scaleFactor];
            UIGraphicsPopContext();
            CGImageRef cgImage = CGBitmapContextCreateImage(cg);
            UIImage* img = [UIImage imageWithCGImage:cgImage scale:[UIScreen mainScreen].scale orientation:UIImageOrientationUp];
            NSData *imgData = UIImagePNGRepresentation(img);
            _result(imgData,nil);
#if !__has_feature(objc_arc)
            [_result release];
#endif
            CGImageRelease(cgImage);
            CGContextRelease(cg);
        }
#else
        CGContextRef bitmapContext = CGBitmapContextCreate(NULL, kOSMScoutDefaultTileSize*_scaleFactor, kOSMScoutDefaultTileSize*_scaleFactor, 8, 0, [[NSColorSpace genericRGBColorSpace] CGColorSpace], kCGBitmapByteOrder32Host|kCGImageAlphaPremultipliedFirst);
        NSGraphicsContext *nsgc = [NSGraphicsContext graphicsContextWithGraphicsPort:bitmapContext flipped:YES];
        [NSGraphicsContext setCurrentContext:nsgc];
        CGContextRef cg = [nsgc graphicsPort];
        CGAffineTransform flipVertical = CGAffineTransformMake(1, 0, 0, -1, 0, kOSMScoutDefaultTileSize*_scaleFactor);
        CGContextConcatCTM(cg, flipVertical);
        [_osmScout drawMapTo:cg lat:_center.latitude lon:_center.longitude zoom:1<<_zoom width:kOSMScoutDefaultTileSize*_scaleFactor height:kOSMScoutDefaultTileSize*_scaleFactor];
        CGImageRef cgImage = CGBitmapContextCreateImage(bitmapContext);
        CGContextRelease(bitmapContext);
        NSBitmapImageRep *bitmapRep = [[NSBitmapImageRep alloc] initWithCGImage:cgImage];
        NSData *imgData = [bitmapRep representationUsingType:NSPNGFileType properties:Nil];
        _result(imgData,nil);
#if !__has_feature(objc_arc)
        [_result release];
#endif
        CGImageRelease(cgImage);
#endif
        [self completeOperation];
        #if !__has_feature(objc_arc)
        [pool release];
        #endif
    }
    @catch(...) {
        // Do not rethrow exceptions.
    }
}


@end

#pragma mark -

@implementation OSMScoutMKTileOverlay
@synthesize path = _path;

static double initialResolution = 2 * M_PI * 6378137 / kOSMScoutDefaultTileSize;
static double originShift = M_PI * 6378137;

-(CLLocationCoordinate2D)centerLatLonForTileX:(NSInteger) tx tileY:(NSInteger) ty zoom:(NSInteger) zoom {
    CLLocationCoordinate2D center;
    double px = (1.0+tx)*kOSMScoutDefaultTileSize;
    double py = (0.0+ty)*kOSMScoutDefaultTileSize;
    
    double res = initialResolution / exp2(zoom);
    double x = px * res - originShift;
    double y = py * res - originShift;
    
    center.longitude = (x / originShift) * 180.0;
    center.latitude = 180 / M_PI * (2 * atan( exp( (y / originShift) * M_PI)) - M_PI_2);
    
    return center;
}

- (void)loadTileAtPath:(MKTileOverlayPath)path result:(void (^)(NSData *tileData, NSError *error))result {
    if(!_osmScout && _path){
        _osmScout = [OSMScout OSMScoutWithPath:_path dpi:220];
        #if !__has_feature(objc_arc)
        [_osmScout retain];
        #endif
        drawQueue = [[NSOperationQueue alloc] init];
        [drawQueue setMaxConcurrentOperationCount:1];
    }
    
    CLLocationCoordinate2D center = [self centerLatLonForTileX:path.x tileY:path.y zoom: path.z];
    OSMScoutMKTileOperation *drawOp = [[OSMScoutMKTileOperation alloc] initWithOsmScout: _osmScout center:center zoom:(path.z-1) scaleFactor:path.contentScaleFactor result:result];
    NSEnumerator *e = drawQueue.operations.reverseObjectEnumerator;
    OSMScoutMKTileOperation *i;
    int count=0;
    while((i = [e nextObject])){
        if(!i.isFinished && !i.isCancelled){
            if(count<RENDER_QUEUE_MAX_LEN-1){
                [i addDependency:drawOp];
            } else {
                [i cancel];
            }
            count++;
        }
    }
    [drawQueue addOperation:drawOp];
    #if !__has_feature(objc_arc)
    [drawOp release];
    #endif
}

@end
