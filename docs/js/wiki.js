/**
 * Infernux Engine - Wiki Page JavaScript
 */

document.addEventListener('DOMContentLoaded', function() {
    const sidebar = document.querySelector('.wiki-sidebar');
    const sidebarLinks = sidebar.querySelectorAll('a');
    const articles = document.querySelectorAll('.wiki-article');
    
    // Update active link on scroll
    function updateActiveLink() {
        const scrollPos = window.scrollY + 100;
        
        articles.forEach(article => {
            const top = article.offsetTop;
            const bottom = top + article.offsetHeight;
            
            if (scrollPos >= top && scrollPos < bottom) {
                const id = article.getAttribute('id');
                
                sidebarLinks.forEach(link => {
                    link.classList.remove('active');
                    if (link.getAttribute('href') === '#' + id) {
                        link.classList.add('active');
                    }
                });
            }
        });
    }
    
    window.addEventListener('scroll', updateActiveLink);
    updateActiveLink();
    
    // Smooth scroll for sidebar links
    sidebarLinks.forEach(link => {
        link.addEventListener('click', function(e) {
            const href = this.getAttribute('href');
            if (href.startsWith('#')) {
                e.preventDefault();
                const target = document.querySelector(href);
                if (target) {
                    const navHeight = document.querySelector('.navbar').offsetHeight;
                    const targetPosition = target.getBoundingClientRect().top + window.pageYOffset - navHeight - 20;
                    
                    window.scrollTo({
                        top: targetPosition,
                        behavior: 'smooth'
                    });
                }
            }
        });
    });
});
