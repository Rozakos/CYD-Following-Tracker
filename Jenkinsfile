// CI for the CYD following tracker: host-side unit tests, then the ESP32 build.
//
// Runs on the self-hosted Jenkins controller (Debian LXC on the Proxmox box),
// which has gcc/g++ and PlatformIO installed system-wide.
//
//   native - Unity unit tests over the interpolation helpers, host gcc, seconds.
//   cyd    - the real ESP32 firmware image.
//
// Note this project uses the min_spiffs partition layout (~1.9 MB app slot),
// unlike CYD-Stock-Ticker which needed huge_app. PlatformIO prints the exact
// budget and usage itself, so the post step reports the artifact size and the
// "Flash: x%" line in the build log is the authoritative check.

pipeline {
  agent any

  options {
    timestamps()
    timeout(time: 30, unit: 'MINUTES')
    disableConcurrentBuilds()
    buildDiscarder(logRotator(numToKeepStr: '20', artifactNumToKeepStr: '10'))
  }

  environment {
    // Keep the ~1.6 GB ESP32 toolchain outside the workspace so cleanWs()
    // does not force a cold re-download every build.
    PLATFORMIO_CORE_DIR = "${JENKINS_HOME}/.platformio"
  }

  stages {
    stage('Checkout') {
      steps {
        checkout scm
        sh 'git --no-pager log -1 --oneline'
      }
    }

    stage('Unit tests (native)') {
      steps {
        sh 'pio test -e native'
      }
    }

    stage('Stub secrets.h') {
      steps {
        // src/secrets.h is gitignored (real WiFi credentials), so a clean clone
        // cannot compile. secrets_example.h is the committed template the README
        // tells humans to copy; CI does the same.
        //
        // This proves the code COMPILES. The artifact carries placeholder
        // credentials and is not a drop-in image for a real network.
        sh '''
          if [ ! -f src/secrets.h ]; then
            cp src/secrets_example.h src/secrets.h
            echo "secrets.h stubbed from secrets_example.h"
          fi
        '''
      }
    }

    stage('Build firmware (cyd)') {
      steps {
        sh 'pio run -e cyd'
      }
    }

    stage('Archive firmware') {
      steps {
        sh 'ls -lh .pio/build/cyd/firmware.bin'
        archiveArtifacts artifacts: '.pio/build/cyd/firmware.*',
                         fingerprint: true,
                         onlyIfSuccessful: true
      }
    }
  }

  post {
    always {
      sh '''
        if [ -f .pio/build/cyd/firmware.bin ]; then
          size=$(stat -c%s .pio/build/cyd/firmware.bin)
          echo "firmware.bin = ${size} bytes ($((size / 1024)) KB) - see the Flash: line above for the partition budget"
        fi
      '''
    }
    cleanup {
      cleanWs()
    }
  }
}
