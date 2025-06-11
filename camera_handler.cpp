#include "camera_handler.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

// PSRAM allocation helper - automatically falls back to DRAM if needed
static void* psram_malloc_prefer(size_t size) {
    void* ret = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (ret == NULL) {
        ret = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    }
    return ret;
}

// Constructor
CameraHandler::CameraHandler() : 
    _scaled_buffer(nullptr),
    _scaled_buffer_size(0),
    _processed_buffer(nullptr),
    _processed_buffer_size(0),
    _current_threshold(128),
    _brightness_ema(128),
    _last_frame_time(0),
    _is_initialized(false) {
    
    // Initialize stats
    _stats.frame_count = 0;
    _stats.total_processing_time = 0;
    _stats.avg_fps = 0;
    _stats.avg_processing_time = 0;
    _stats.start_time = millis();
    _stats.frame_drops = 0;
    _stats.memory_usage = 0;
    
    // Default settings
    _settings.frame_size = CAMERA_SIZE_QVGA;
    _settings.quality = CAMERA_QUALITY_BALANCED;
    _settings.process_mode = CAMERA_MODE_GRAYSCALE;
    _settings.contrast = 1;
    _settings.brightness = 0;
    _settings.threshold = 128;
    _settings.auto_exposure = true;
    _settings.awb = true;
    _settings.high_speed = false;
}

// Destructor - Clean up memory
CameraHandler::~CameraHandler() {
    if (_scaled_buffer) {
        free(_scaled_buffer);
        _scaled_buffer = nullptr;
    }
    
    if (_processed_buffer) {
        free(_processed_buffer);
        _processed_buffer = nullptr;
    }
    
    esp_camera_deinit();
}

// Initialize camera with custom settings
bool CameraHandler::begin(camera_config_settings_t settings) {
    _settings = settings;
    
    // Set up camera configuration
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    
    // Use the standardized XIAO ESP32S3 pin definitions
    config.pin_d0 = XIAO_CAM_Y2;
    config.pin_d1 = XIAO_CAM_Y3;
    config.pin_d2 = XIAO_CAM_Y4;
    config.pin_d3 = XIAO_CAM_Y5;
    config.pin_d4 = XIAO_CAM_Y6;
    config.pin_d5 = XIAO_CAM_Y7;
    config.pin_d6 = XIAO_CAM_Y8;
    config.pin_d7 = XIAO_CAM_Y9;
    config.pin_xclk = XIAO_CAM_XCLK;
    config.pin_pclk = XIAO_CAM_PCLK;
    config.pin_vsync = XIAO_CAM_VSYNC;
    config.pin_href = XIAO_CAM_HREF;
    config.pin_sscb_sda = XIAO_CAM_SIOD;
    config.pin_sscb_scl = XIAO_CAM_SIOC;
    config.pin_pwdn = XIAO_CAM_PWDN;
    config.pin_reset = XIAO_CAM_RESET;
    
    // Set XCLK frequency - lower can improve stability
    config.xclk_freq_hz = 20000000;
    
    // Set pixel format to grayscale for efficient processing
    config.pixel_format = PIXFORMAT_GRAYSCALE;
    
    // Set frame size based on settings
    switch (_settings.frame_size) {
        case CAMERA_SIZE_QQVGA:
            config.frame_size = FRAMESIZE_QQVGA;  // 160x120
            break;
        case CAMERA_SIZE_VGA:
            config.frame_size = FRAMESIZE_VGA;    // 640x480
            break;
        case CAMERA_SIZE_QVGA:
        default:
            config.frame_size = FRAMESIZE_QVGA;   // 320x240
            break;
    }
    
    // Configure frame buffer count and location based on available memory
    if (psramFound()) {
        config.fb_count = 2;
        config.fb_location = CAMERA_FB_IN_PSRAM;
        config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_DRAM;
        config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    }
    
    // Initialize camera
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera initialization failed with error 0x%x\n", err);
        return false;
    }
    
    // Apply camera settings
    sensor_t* sensor = esp_camera_sensor_get();
    if (sensor) {
        sensor->set_brightness(sensor, _settings.brightness);
        sensor->set_contrast(sensor, _settings.contrast);
        sensor->set_saturation(sensor, 0); // Not used in grayscale
        sensor->set_special_effect(sensor, 0); // No special effects
        sensor->set_whitebal(sensor, _settings.awb);
        sensor->set_awb_gain(sensor, _settings.awb);
        sensor->set_wb_mode(sensor, 0); // Auto white balance
        sensor->set_exposure_ctrl(sensor, _settings.auto_exposure);
        sensor->set_aec2(sensor, _settings.auto_exposure);
        sensor->set_gain_ctrl(sensor, _settings.auto_exposure);
        sensor->set_gainceiling(sensor, (gainceiling_t)2); // Moderate gain ceiling
    }
    
    // Allocate buffer for scaled image (160x128 is a good balance for a small display)
    _scaled_buffer_size = 160 * 128;
    _scaled_buffer = (uint8_t*)psram_malloc_prefer(_scaled_buffer_size);
    
    // Allocate buffer for processed image
    _processed_buffer_size = 160 * 128;
    _processed_buffer = (uint8_t*)psram_malloc_prefer(_processed_buffer_size);
    
    // Check if buffer allocation was successful
    if (_scaled_buffer == nullptr || _processed_buffer == nullptr) {
        Serial.println("Failed to allocate camera buffers");
        return false;
    }
    
    // Clear buffers
    memset(_scaled_buffer, 0, _scaled_buffer_size);
    memset(_processed_buffer, 0, _processed_buffer_size);
    
    // Update initialization status
    _is_initialized = true;
    _stats.start_time = millis();
    _stats.memory_usage = _scaled_buffer_size + _processed_buffer_size;
    
    return true;
}

// Capture a frame from the camera
camera_fb_t* CameraHandler::captureFrame() {
    if (!_is_initialized) {
        return nullptr;
    }
    
    // Try to capture a frame
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb == nullptr) {
        _stats.frame_drops++;
        return nullptr;
    }
    
    // Update timing stats
    uint32_t current_time = millis();
    if (_last_frame_time > 0) {
        float frame_time = current_time - _last_frame_time;
        if (_stats.frame_count > 0) {
            _stats.avg_fps = ((_stats.avg_fps * (_stats.frame_count - 1)) + (1000.0f / frame_time)) / _stats.frame_count;
        } else {
            _stats.avg_fps = 1000.0f / frame_time;
        }
    }
    _last_frame_time = current_time;
    
    return fb;
}

// Process a captured frame
bool CameraHandler::processFrame(camera_fb_t* fb, int target_width, int target_height) {
    if (!_is_initialized || fb == nullptr) {
        return false;
    }
    
    uint32_t start_time = millis();
    
    // First scale the image to target size
    scaleImage(fb->buf, fb->width, fb->height, _scaled_buffer, target_width, target_height);
    
    // Apply processing based on selected mode
    switch (_settings.process_mode) {
        case CAMERA_MODE_BINARY:
            applyBinaryThreshold(_scaled_buffer, _processed_buffer, target_width, target_height, _settings.threshold);
            break;
            
        case CAMERA_MODE_ADAPTIVE:
            updateDynamicThreshold(fb);
            applyAdaptiveThreshold(_scaled_buffer, _processed_buffer, target_width, target_height);
            break;
            
        case CAMERA_MODE_EDGE:
            applyEdgeDetection(_scaled_buffer, _processed_buffer, target_width, target_height);
            break;
            
        case CAMERA_MODE_INVERTED:
            applyInversion(_scaled_buffer, _processed_buffer, target_width, target_height);
            break;
            
        case CAMERA_MODE_GRAYSCALE:
        default:
            // For grayscale mode, apply dithering for display
            applyDithering(_scaled_buffer, _processed_buffer, target_width, target_height);
            break;
    }
    
    // Update performance stats
    uint32_t processing_time = millis() - start_time;
    _stats.total_processing_time += processing_time;
    _stats.frame_count++;
    _stats.avg_processing_time = (float)_stats.total_processing_time / _stats.frame_count;
    
    // Adjust quality based on performance if high speed mode is enabled
    if (_settings.high_speed) {
        adaptQualityToPerformance();
    }
    
    return true;
}

// Scaling function optimized for speed
void CameraHandler::scaleImage(uint8_t* src, int src_width, int src_height, 
                             uint8_t* dst, int dst_width, int dst_height) {
    // Calculate scaling factors
    float x_ratio = (float)src_width / dst_width;
    float y_ratio = (float)src_height / dst_height;
    
    // Fast scaling with nearest neighbor
    for (int y = 0; y < dst_height; y++) {
        int src_y = (int)(y * y_ratio);
        if (src_y >= src_height) src_y = src_height - 1;
        
        for (int x = 0; x < dst_width; x++) {
            int src_x = (int)(x * x_ratio);
            if (src_x >= src_width) src_x = src_width - 1;
            
            dst[y * dst_width + x] = src[src_y * src_width + src_x];
        }
    }
}

// Update the dynamic threshold based on frame brightness
void CameraHandler::updateDynamicThreshold(camera_fb_t* fb) {
    // Calculate average brightness with Exponential Moving Average
    const float alpha = 0.2f; // Smoothing factor
    int sum = 0;
    int sample_count = 0;
    
    // Sample every 16th pixel for efficiency
    for (int i = 0; i < fb->width * fb->height; i += 16) {
        sum += fb->buf[i];
        sample_count++;
    }
    
    float avg_brightness = (float)sum / sample_count;
    _brightness_ema = alpha * avg_brightness + (1.0f - alpha) * _brightness_ema;
    
    // Adjust threshold based on average brightness
    if (_brightness_ema < 80) {
        // Low light - use lower threshold
        _current_threshold = _settings.threshold * 0.75;
    } else if (_brightness_ema > 180) {
        // Bright scene - use higher threshold
        _current_threshold = _settings.threshold * 1.25;
    } else {
        // Normal lighting - use normal threshold
        _current_threshold = _settings.threshold;
    }
    
    // Clamp threshold value
    if (_current_threshold < 60) _current_threshold = 60;
    if (_current_threshold > 200) _current_threshold = 200;
}

// Apply adaptive thresholding to the image
void CameraHandler::applyAdaptiveThreshold(uint8_t* src, uint8_t* dst, int width, int height) {
    // Window size for local thresholding
    const int window = 8;
    const int half_window = window / 2;
    
    // Process each pixel
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            // Calculate local average
            int sum = 0;
            int count = 0;
            
            for (int wy = -half_window; wy <= half_window; wy++) {
                int py = y + wy;
                if (py < 0 || py >= height) continue;
                
                for (int wx = -half_window; wx <= half_window; wx++) {
                    int px = x + wx;
                    if (px < 0 || px >= width) continue;
                    
                    sum += src[py * width + px];
                    count++;
                }
            }
            
            // Calculate local threshold
            uint8_t local_threshold = (sum / count) - 10; // Slight offset for better results
            
            // Apply threshold
            uint8_t pixel = src[y * width + x];
            dst[y * width + x] = (pixel > local_threshold) ? 255 : 0;
        }
    }
}

// Apply binary thresholding to the image
void CameraHandler::applyBinaryThreshold(uint8_t* src, uint8_t* dst, int width, int height, uint8_t threshold) {
    for (int i = 0; i < width * height; i++) {
        dst[i] = (src[i] > threshold) ? 255 : 0;
    }
}

// Apply simple edge detection
void CameraHandler::applyEdgeDetection(uint8_t* src, uint8_t* dst, int width, int height) {
    // Simple Sobel-like edge detection
    memset(dst, 0, width * height);
    
    for (int y = 1; y < height - 1; y++) {
        for (int x = 1; x < width - 1; x++) {
            int gx = 
                -1 * src[(y-1) * width + (x-1)] + 
                 1 * src[(y-1) * width + (x+1)] + 
                -2 * src[y     * width + (x-1)] + 
                 2 * src[y     * width + (x+1)] + 
                -1 * src[(y+1) * width + (x-1)] + 
                 1 * src[(y+1) * width + (x+1)];
                 
            int gy = 
                -1 * src[(y-1) * width + (x-1)] + 
                -2 * src[(y-1) * width + x    ] + 
                -1 * src[(y-1) * width + (x+1)] + 
                 1 * src[(y+1) * width + (x-1)] + 
                 2 * src[(y+1) * width + x    ] + 
                 1 * src[(y+1) * width + (x+1)];
                 
            int magnitude = sqrt((gx * gx) + (gy * gy));
            if (magnitude > _settings.threshold) {
                dst[y * width + x] = 255;
            }
        }
    }
}

// Apply dithering for grayscale simulation
void CameraHandler::applyDithering(uint8_t* src, uint8_t* dst, int width, int height) {
    // Bayer dithering matrix 4x4
    static const uint8_t bayer4x4[16] = {
        0,  8,  2,  10,
        12, 4,  14, 6,
        3,  11, 1,  9,
        15, 7,  13, 5
    };
    
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint8_t pixel = src[y * width + x];
            
            // Get the threshold from the Bayer matrix
            uint8_t threshold = bayer4x4[(y % 4) * 4 + (x % 4)] * 16;
            
            // Apply threshold
            dst[y * width + x] = (pixel > threshold) ? 255 : 0;
        }
    }
}

// Apply inversion to the image
void CameraHandler::applyInversion(uint8_t* src, uint8_t* dst, int width, int height) {
    for (int i = 0; i < width * height; i++) {
        dst[i] = 255 - src[i];
    }
}

// Adapt quality settings based on performance
void CameraHandler::adaptQualityToPerformance() {
    if (_stats.avg_processing_time > 50) {
        // Slow performance, reduce quality
        if (_settings.frame_size != CAMERA_SIZE_QQVGA) {
            _settings.frame_size = CAMERA_SIZE_QQVGA;
            setFrameSize(_settings.frame_size);
        }
        if (_settings.quality != CAMERA_QUALITY_FAST) {
            _settings.quality = CAMERA_QUALITY_FAST;
        }
    } else if (_stats.avg_processing_time < 20) {
        // Fast performance, can increase quality
        if (_settings.frame_size == CAMERA_SIZE_QQVGA) {
            _settings.frame_size = CAMERA_SIZE_QVGA;
            setFrameSize(_settings.frame_size);
        }
        if (_settings.quality == CAMERA_QUALITY_FAST) {
            _settings.quality = CAMERA_QUALITY_BALANCED;
        }
    }
}

// Set the frame size
void CameraHandler::setFrameSize(camera_frame_size_t size) {
    if (!_is_initialized) return;
    
    _settings.frame_size = size;
    sensor_t* sensor = esp_camera_sensor_get();
    if (sensor) {
        switch (size) {
            case CAMERA_SIZE_QQVGA:
                sensor->set_framesize(sensor, FRAMESIZE_QQVGA);
                break;
            case CAMERA_SIZE_VGA:
                sensor->set_framesize(sensor, FRAMESIZE_VGA);
                break;
            case CAMERA_SIZE_QVGA:
            default:
                sensor->set_framesize(sensor, FRAMESIZE_QVGA);
                break;
        }
    }
}

// Set the processing quality
void CameraHandler::setQuality(camera_quality_t quality) {
    _settings.quality = quality;
}

// Set the processing mode
void CameraHandler::setProcessMode(camera_process_mode_t mode) {
    _settings.process_mode = mode;
}

// Set the threshold value
void CameraHandler::setThreshold(uint8_t threshold) {
    _settings.threshold = threshold;
}

// Set the contrast value
void CameraHandler::setContrast(uint8_t contrast) {
    if (!_is_initialized) return;
    
    _settings.contrast = contrast;
    sensor_t* sensor = esp_camera_sensor_get();
    if (sensor) {
        sensor->set_contrast(sensor, contrast);
    }
}

// Set the brightness value
void CameraHandler::setBrightness(uint8_t brightness) {
    if (!_is_initialized) return;
    
    _settings.brightness = brightness;
    sensor_t* sensor = esp_camera_sensor_get();
    if (sensor) {
        sensor->set_brightness(sensor, brightness);
    }
}

// Enable/disable auto exposure
void CameraHandler::setAutoExposure(bool enabled) {
    if (!_is_initialized) return;
    
    _settings.auto_exposure = enabled;
    sensor_t* sensor = esp_camera_sensor_get();
    if (sensor) {
        sensor->set_exposure_ctrl(sensor, enabled);
        sensor->set_aec2(sensor, enabled);
        sensor->set_gain_ctrl(sensor, enabled);
    }
}

// Enable/disable auto white balance
void CameraHandler::setAutoWhiteBalance(bool enabled) {
    if (!_is_initialized) return;
    
    _settings.awb = enabled;
    sensor_t* sensor = esp_camera_sensor_get();
    if (sensor) {
        sensor->set_whitebal(sensor, enabled);
        sensor->set_awb_gain(sensor, enabled);
    }
}

// Enable/disable high speed mode
void CameraHandler::setHighSpeedMode(bool enabled) {
    _settings.high_speed = enabled;
}

// Get statistics
camera_stats_t CameraHandler::getStats() {
    // Update fps calculation
    uint32_t elapsed_time = (millis() - _stats.start_time) / 1000;
    if (elapsed_time > 0) {
        _stats.avg_fps = (float)_stats.frame_count / elapsed_time;
    }
    
    return _stats;
}

// Reset statistics
void CameraHandler::resetStats() {
    _stats.frame_count = 0;
    _stats.total_processing_time = 0;
    _stats.avg_fps = 0;
    _stats.avg_processing_time = 0;
    _stats.start_time = millis();
    _stats.frame_drops = 0;
}

// Get current FPS
float CameraHandler::getCurrentFPS() {
    uint32_t elapsed_time = (millis() - _stats.start_time) / 1000;
    if (elapsed_time > 0) {
        return (float)_stats.frame_count / elapsed_time;
    }
    return 0;
}

// Get average processing time
float CameraHandler::getAverageProcessingTime() {
    return _stats.avg_processing_time;
}

// Check if camera is initialized
bool CameraHandler::isInitialized() {
    return _is_initialized;
}

// Get processed buffer
uint8_t* CameraHandler::getProcessedBuffer() {
    return _processed_buffer;
}

// Get scaled buffer
uint8_t* CameraHandler::getScaledBuffer() {
    return _scaled_buffer;
}
