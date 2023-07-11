from setuptools import setup, find_packages

setup(
    name='hydro_data_view',
    version='0.1.0',
    long_description='A way to view hydroponic data',
    packages=find_packages(),
    include_package_data=True,
    zip_safe=False,
    install_requires=['Flask', 'mysql-connector-python']
)


