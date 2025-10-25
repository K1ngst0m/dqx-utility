"""
Cross-compile Windows binaries using llvm-mingw in a container.
Works with both Docker and Podman (auto-detected).
"""

import argparse
import os
import platform
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path


class Colors:
    """ANSI color codes for terminal output."""
    RED = '\033[0;31m'
    GREEN = '\033[0;32m'
    YELLOW = '\033[1;33m'
    BLUE = '\033[0;34m'
    NC = '\033[0m'  # No Color

    @staticmethod
    def strip_if_no_tty():
        """Disable colors if not running in a TTY."""
        if not sys.stdout.isatty():
            Colors.RED = ''
            Colors.GREEN = ''
            Colors.YELLOW = ''
            Colors.BLUE = ''
            Colors.NC = ''


Colors.strip_if_no_tty()


def log_info(msg):
    print(f"{Colors.BLUE}[INFO]{Colors.NC} {msg}")


def log_success(msg):
    print(f"{Colors.GREEN}[SUCCESS]{Colors.NC} {msg}")


def log_warning(msg):
    print(f"{Colors.YELLOW}[WARNING]{Colors.NC} {msg}")


def log_error(msg):
    print(f"{Colors.RED}[ERROR]{Colors.NC} {msg}", file=sys.stderr)


class ContainerBuilder:
    """Handles cross-compilation using llvm-mingw container."""

    def __init__(self, args):
        self.args = args
        self.container_cmd = self._detect_container_runtime()
        self.script_dir = Path(__file__).parent.resolve()
        self.project_root = self.script_dir.parent
        self.preset = args.preset
        self.build_type = args.build_type
        self.jobs = args.jobs if args.jobs else os.cpu_count() or 4
        self.image = args.image
        self.enable_logging = not args.no_log
        self.log_dir = self.project_root / 'build-logs'

        # Create log directory if logging is enabled
        if self.enable_logging:
            self.log_dir.mkdir(exist_ok=True)

    @staticmethod
    def _detect_container_runtime():
        """Detect available container runtime (podman or docker)."""
        if shutil.which('podman'):
            return 'podman'
        elif shutil.which('docker'):
            return 'docker'
        else:
            log_error("Neither podman nor docker found in PATH")
            sys.exit(1)

    def _get_container_flags(self):
        """Get container runtime-specific flags."""
        flags = []

        if self.container_cmd == 'podman':
            # Podman-specific: preserve user namespace
            flags.append('--userns=keep-id')

            # SELinux relabeling for volume mounts (if on SELinux-enabled system)
            if platform.system() == 'Linux' and Path('/sys/fs/selinux').exists():
                return flags, ':z'  # Use lowercase 'z' for shared volume label
            return flags, ''
        else:
            # Docker
            return flags, ''

    def run_container(self, cmd, interactive=True, log_file=None):
        """Run command in container with proper mounts and environment."""
        runtime_flags, volume_suffix = self._get_container_flags()

        container_args = [
            self.container_cmd,
            'run',
            '--rm',
        ]

        # Add interactive/TTY flags
        if interactive:
            container_args.append('-it')

        # Add runtime-specific flags
        container_args.extend(runtime_flags)

        # Add volume mounts and working directory
        container_args.extend([
            '-v', f"{self.project_root}:/work{volume_suffix}",
            '-w', '/work',
        ])

        # Add environment variables
        container_args.extend([
            '-e', f'PRESET={self.preset}',
            '-e', f'BUILD_TYPE={self.build_type}',
            '-e', f'JOBS={self.jobs}',
        ])

        # Add image and command
        container_args.extend([
            self.image,
            'bash', '-c', cmd
        ])

        log_info(f"Running: {' '.join(container_args)}")

        # Run with or without logging
        if log_file and self.enable_logging:
            return self._run_with_logging(container_args, log_file)
        else:
            return subprocess.run(
                container_args,
                cwd=self.project_root,
                check=False  # Don't raise exception, let caller handle errors
            )

    def _run_with_logging(self, container_args, log_file):
        """Run command with output to console, filter warnings/errors to log file."""
        log_path = self.log_dir / log_file

        log_info(f"Logging warnings/errors to: {log_path}")

        # Write header to log file
        timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        header = f"=== Build Log (Warnings and Errors Only) ===\n"
        header += f"Timestamp: {timestamp}\n"
        header += f"Preset: {self.preset}\n"
        header += f"Command: {' '.join(container_args)}\n"
        header += f"{'='*80}\n\n"

        with open(log_path, 'w', encoding='utf-8') as f:
            f.write(header)

        # Platform-specific logging implementation
        if platform.system() == 'Windows':
            returncode = self._run_with_logging_windows(container_args, log_path)
        else:
            returncode = self._run_with_logging_unix(container_args, log_path)

        # Write footer
        with open(log_path, 'a', encoding='utf-8') as f:
            f.write(f"\n{'='*80}\n")
            f.write(f"Exit code: {returncode}\n")

        return subprocess.CompletedProcess(
            args=container_args,
            returncode=returncode
        )

    def _run_with_logging_unix(self, container_args, log_path):
        """Unix/Linux logging using Bash pipeline with tee and process substitution."""
        cmd_str = ' '.join(f'"{arg}"' if ' ' in arg else arg for arg in container_args)
        grep_pattern = 'warning:\\|error:\\|Error:\\|ERROR:\\|fatal error:'

        pipeline = f"{cmd_str} 2>&1 | tee >(grep -i '{grep_pattern}' >> '{log_path}' || true)"

        process = subprocess.Popen(
            pipeline,
            shell=True,
            executable='/bin/bash',
            cwd=self.project_root
        )

        try:
            returncode = process.wait()
        except KeyboardInterrupt:
            process.terminate()
            process.wait()
            raise

        return returncode

    def _run_with_logging_windows(self, container_args, log_path):
        """Windows logging using Python-based output filtering."""
        import re

        # Compile regex pattern for warnings/errors
        error_pattern = re.compile(
            r'(warning:|error:|Error:|ERROR:|fatal error:)',
            re.IGNORECASE
        )

        process = subprocess.Popen(
            container_args,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding='utf-8',
            errors='replace',
            bufsize=1,  # Line buffered
            cwd=self.project_root
        )

        try:
            with open(log_path, 'a', encoding='utf-8') as log_f:
                for line in process.stdout:
                    # Print to console in real-time
                    print(line, end='', flush=True)

                    # Filter and write warnings/errors to log file
                    if error_pattern.search(line):
                        log_f.write(line)
                        log_f.flush()

            returncode = process.wait()
        except KeyboardInterrupt:
            process.terminate()
            process.wait()
            raise

        return returncode

    def configure(self):
        """Configure build with CMake."""
        log_info(f"Configuring with preset: {self.preset}")
        timestamp = datetime.now().strftime('%Y%m%d-%H%M%S')
        log_file = f"configure-{self.preset}-{timestamp}.log"

        result = self.run_container(
            f"cmake --preset {self.preset}",
            interactive=True,
            log_file=log_file
        )

        if result.returncode == 0:
            log_success("Configuration complete")
        else:
            log_error("Configuration failed")
            if self.enable_logging:
                log_error(f"Check log: {self.log_dir / log_file}")
            sys.exit(1)

    def build(self):
        """Build project."""
        log_info(f"Building with preset: {self.preset} ({self.jobs} parallel jobs)")
        timestamp = datetime.now().strftime('%Y%m%d-%H%M%S')
        log_file = f"build-{self.preset}-{timestamp}.log"

        result = self.run_container(
            f"cmake --build --preset {self.preset} -j{self.jobs}",
            interactive=True,
            log_file=log_file
        )

        if result.returncode == 0:
            log_success("Build complete")
            log_info(f"Output: {self.project_root}/out/{self.preset}/")
            if self.enable_logging:
                log_info(f"Build log: {self.log_dir / log_file}")
        else:
            log_error("Build failed")
            if self.enable_logging:
                log_error(f"Check log: {self.log_dir / log_file}")
            sys.exit(1)

    def clean(self):
        """Clean build artifacts."""
        log_warning(f"Cleaning build artifacts for preset: {self.preset}")
        build_dir = self.project_root / 'out' / self.preset
        if build_dir.exists():
            shutil.rmtree(build_dir)
            log_success(f"Cleaned: out/{self.preset}")
        else:
            log_info(f"Nothing to clean (out/{self.preset} doesn't exist)")

    def shell(self):
        """Open interactive shell in container."""
        log_info("Opening interactive shell in container")
        log_info(f"Container runtime: {self.container_cmd}")
        log_info("Project mounted at: /work")
        log_info("Toolchain: x86_64-w64-mingw32-clang/clang++")
        print()
        self.run_container("bash", interactive=True)

    def package(self):
        """Create portable package."""
        log_info(f"Creating portable package with preset: {self.preset}")
        timestamp = datetime.now().strftime('%Y%m%d-%H%M%S')
        log_file = f"package-{self.preset}-{timestamp}.log"

        result = self.run_container(
            f"cmake --build out/{self.preset} --target package",
            interactive=True,
            log_file=log_file
        )

        if result.returncode == 0:
            log_success(f"Package created in: out/{self.preset}/")

            # List generated packages
            log_info("Generated packages:")
            build_dir = self.project_root / 'out' / self.preset
            for ext in ['*.zip', '*.tar.gz']:
                for package in build_dir.glob(ext):
                    print(f"  - {package}")
        else:
            log_error("Packaging failed")
            if self.enable_logging:
                log_error(f"Check log: {self.log_dir / log_file}")
            sys.exit(1)

    def print_info(self):
        """Print configuration information."""
        log_info(f"Container runtime: {self.container_cmd}")
        log_info(f"Container image: {self.image}")
        log_info(f"CMake preset: {self.preset}")
        log_info(f"Build type: {self.build_type}")
        log_info(f"Parallel jobs: {self.jobs}")
        if self.enable_logging:
            log_info(f"Build logs: {self.log_dir}/")
        else:
            log_info("Build logging: disabled")
        print()


def main():
    parser = argparse.ArgumentParser(
        description='Cross-compile Windows binaries using llvm-mingw in a container.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
EXAMPLES:
    # Build release version (default)
    ./scripts/build-mingw.py

    # Build debug version
    ./scripts/build-mingw.py -p llvm-mingw-cross-debug

    # Configure only
    ./scripts/build-mingw.py configure

    # Build with 8 parallel jobs
    ./scripts/build-mingw.py -j 8 build

    # Open shell for debugging
    ./scripts/build-mingw.py shell

    # Create portable package
    ./scripts/build-mingw.py package

    # Full workflow: clean, configure, build, package
    ./scripts/build-mingw.py clean configure build package
        """
    )

    parser.add_argument(
        '-p', '--preset',
        default=os.environ.get('PRESET', 'llvm-mingw-cross-release'),
        help='CMake preset to use (default: llvm-mingw-cross-release)'
    )
    parser.add_argument(
        '-j', '--jobs',
        type=int,
        help='Number of parallel jobs (default: auto-detected)'
    )
    parser.add_argument(
        '-i', '--image',
        default='docker.io/mstorsjo/llvm-mingw:latest',
        help='Container image to use (default: docker.io/mstorsjo/llvm-mingw:latest)'
    )
    parser.add_argument(
        '--build-type',
        default=os.environ.get('BUILD_TYPE', 'Release'),
        choices=['Debug', 'Release'],
        help='Build type (default: Release)'
    )
    parser.add_argument(
        '--no-log',
        action='store_true',
        help='Disable logging to file (only output to console)'
    )
    parser.add_argument(
        'commands',
        nargs='*',
        choices=['configure', 'build', 'clean', 'shell', 'package'],
        help='Commands to run (default: configure build)'
    )

    args = parser.parse_args()

    # Default commands if none specified
    if not args.commands:
        args.commands = ['configure', 'build']

    builder = ContainerBuilder(args)
    builder.print_info()

    # Execute commands in order
    for cmd in args.commands:
        if cmd == 'configure':
            builder.configure()
        elif cmd == 'build':
            builder.build()
        elif cmd == 'clean':
            builder.clean()
        elif cmd == 'shell':
            builder.shell()
        elif cmd == 'package':
            builder.package()


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        log_warning("Interrupted by user")
        sys.exit(130)
    except Exception as e:
        log_error(f"Unexpected error: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
