import AVFoundation
import Accelerate
import Foundation

final class FFTAnalyzer {
    private let size = 2048
    private let log2Size = vDSP_Length(11)
    private var setup: FFTSetup?
    private var window: [Float]

    init() {
        setup = vDSP_create_fftsetup(log2Size, FFTRadix(kFFTRadix2))
        window = [Float](repeating: 0, count: size)
        vDSP_hann_window(&window, vDSP_Length(size), Int32(vDSP_HANN_NORM))
    }

    deinit {
        if let setup { vDSP_destroy_fftsetup(setup) }
    }

    func analyze(_ buffer: AVAudioPCMBuffer) -> [Float] {
        guard let setup, let channel = buffer.floatChannelData?[0] else {
            return [Float](repeating: 0, count: 72)
        }

        var samples = [Float](repeating: 0, count: size)
        let count = min(Int(buffer.frameLength), size)
        for i in 0..<count { samples[i] = channel[i] }
        vDSP_vmul(samples, 1, window, 1, &samples, 1, vDSP_Length(size))

        var real = [Float](repeating: 0, count: size / 2)
        var imag = [Float](repeating: 0, count: size / 2)
        var magnitudes = [Float](repeating: 0, count: size / 2)

        real.withUnsafeMutableBufferPointer { realBuffer in
            imag.withUnsafeMutableBufferPointer { imagBuffer in
                var split = DSPSplitComplex(realp: realBuffer.baseAddress!, imagp: imagBuffer.baseAddress!)
                samples.withUnsafeBufferPointer { sampleBuffer in
                    sampleBuffer.baseAddress!.withMemoryRebound(to: DSPComplex.self, capacity: size / 2) { complex in
                        vDSP_ctoz(complex, 2, &split, 1, vDSP_Length(size / 2))
                    }
                }
                vDSP_fft_zrip(setup, &split, 1, log2Size, FFTDirection(FFT_FORWARD))
                vDSP_zvmags(&split, 1, &magnitudes, 1, vDSP_Length(size / 2))
            }
        }

        let maxBin = magnitudes.count - 1
        var output = [Float](repeating: 0, count: 72)
        let logMax = log(Double(maxBin))
        for band in 0..<72 {
            let a = Double(band) / 72.0
            let b = Double(band + 1) / 72.0
            let low = max(1, Int(exp(a * logMax)))
            let high = max(low + 1, min(maxBin, Int(exp(b * logMax))))
            var sum: Float = 0
            if low < high {
                for bin in low..<high { sum += magnitudes[bin] }
                sum /= Float(high - low)
            }
            let amplitude = sqrt(max(sum, 0)) / Float(size)
            let db = 20 * log10(max(amplitude, 0.000_001))
            output[band] = min(1, max(0, (db + 72) / 72))
        }
        return output
    }
}

final class AudioEngine: ObservableObject {
    @Published private(set) var isPlaying = false
    @Published private(set) var position: Double = 0
    @Published private(set) var duration: Double = 0
    @Published private(set) var bands: [Float] = [Float](repeating: 0, count: 72)
    @Published var volume: Float = 0.82 {
        didSet { player.volume = volume }
    }

    var onFinished: (() -> Void)?

    private let engine = AVAudioEngine()
    private let player = AVAudioPlayerNode()
    private let analyzer = FFTAnalyzer()
    private var file: AVAudioFile?
    private var startFrame: AVAudioFramePosition = 0
    private var sampleRate: Double = 44_100
    private var timer: Timer?
    private var finishSent = false

    init() {
        configureSession()
        engine.attach(player)
        engine.connect(player, to: engine.mainMixerNode, format: nil)
        installAnalyzerTap()
        engine.prepare()
        do { try engine.start() } catch { print("EtherPlayer audio engine start failed: \(error)") }
        player.volume = volume
        timer = Timer.scheduledTimer(withTimeInterval: 0.08, repeats: true) { [weak self] _ in self?.tick() }
    }

    deinit {
        timer?.invalidate()
        engine.mainMixerNode.removeTap(onBus: 0)
        engine.stop()
    }

    func load(url: URL, autoplay: Bool = true) throws {
        player.stop()
        let opened = try AVAudioFile(forReading: url)
        file = opened
        sampleRate = opened.processingFormat.sampleRate
        duration = Double(opened.length) / sampleRate
        position = 0
        startFrame = 0
        finishSent = false
        schedule(from: 0)
        if autoplay { play() } else { isPlaying = false }
    }

    func play() {
        guard file != nil else { return }
        if !engine.isRunning { try? engine.start() }
        player.play()
        isPlaying = true
        finishSent = false
    }

    func pause() {
        guard file != nil else { return }
        let frame = currentFrame()
        player.stop()
        startFrame = frame
        schedule(from: frame)
        position = Double(frame) / sampleRate
        isPlaying = false
    }

    func toggle() { isPlaying ? pause() : play() }

    func stop() {
        player.stop()
        startFrame = 0
        position = 0
        isPlaying = false
        finishSent = false
        if file != nil { schedule(from: 0) }
    }

    func seek(to seconds: Double) {
        guard let file else { return }
        let targetSeconds = min(max(0, seconds), duration)
        let target = min(file.length, AVAudioFramePosition(targetSeconds * sampleRate))
        let resume = isPlaying
        player.stop()
        startFrame = target
        position = targetSeconds
        schedule(from: target)
        if resume { player.play() }
    }

    private func schedule(from frame: AVAudioFramePosition) {
        guard let file else { return }
        let remaining = max(0, file.length - frame)
        guard remaining > 0 else { return }
        player.scheduleSegment(file, startingFrame: frame, frameCount: AVAudioFrameCount(min(remaining, AVAudioFramePosition(UInt32.max))), at: nil)
    }

    private func currentFrame() -> AVAudioFramePosition {
        guard let render = player.lastRenderTime,
              let time = player.playerTime(forNodeTime: render) else { return startFrame }
        return min(file?.length ?? startFrame, startFrame + time.sampleTime)
    }

    private func tick() {
        guard file != nil else { return }
        let seconds = Double(currentFrame()) / sampleRate
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            self.position = min(self.duration, seconds)
            if self.isPlaying, self.duration > 0, self.position >= self.duration - 0.08, !self.finishSent {
                self.finishSent = true
                self.isPlaying = false
                self.onFinished?()
            }
        }
    }

    private func installAnalyzerTap() {
        let mixer = engine.mainMixerNode
        mixer.installTap(onBus: 0, bufferSize: 2048, format: nil) { [weak self] buffer, _ in
            guard let self else { return }
            let next = self.analyzer.analyze(buffer)
            DispatchQueue.main.async {
                guard self.bands.count == next.count else { self.bands = next; return }
                for i in next.indices {
                    let rising = next[i] > self.bands[i]
                    let blend: Float = rising ? 0.58 : 0.18
                    self.bands[i] += (next[i] - self.bands[i]) * blend
                }
            }
        }
    }

    private func configureSession() {
        do {
            let session = AVAudioSession.sharedInstance()
            try session.setCategory(.playback, mode: .default, options: [])
            try session.setActive(true)
        } catch {
            print("EtherPlayer audio session failed: \(error)")
        }
    }
}
